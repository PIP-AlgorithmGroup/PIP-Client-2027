/** 复现独立规则动画，验证归属阈值和不变形外框；通过参数指定临时帧目录。 */
const fs = require("node:fs/promises");
const path = require("node:path");
const assert = require("node:assert/strict");
const { chromium } = require("playwright");

(async () => {
    const root = __dirname;
    const frame_dir = process.argv[2];
    if (!frame_dir) throw new Error("Provide an absolute directory for temporary animation frames");
    const source = await fs.readFile(path.join(root, "control_transition.html"), "utf8");
    const launch = { headless: true };
    if (process.env.PREVIEW_BROWSER_PATH) launch.executablePath = process.env.PREVIEW_BROWSER_PATH;
    else if (process.env.PREVIEW_BROWSER) launch.channel = process.env.PREVIEW_BROWSER;
    const browser = await chromium.launch(launch);
    try {
        const page = await browser.newPage({ viewport: { width: 736, height: 340 }, deviceScaleFactor: 1 });
        const errors = [], requests = [];
        page.on("pageerror", error => errors.push(error.message));
        page.on("request", request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
        await page.setContent(`<html style="color-scheme:dark"><body style="margin:0">${source}</body></html>`);
        const states = await page.evaluate(() => {
            const model = window.__controlTransition;
            return Object.fromEntries(["B", "C", "R"].map(id => [id, Object.fromEntries(
                [0, 2.5, 4.9, 5, 6, 7, 9.9, 10, 11, 12, 14.9, 15, 16, 17, 18, 19, 20, 21, 24].map(time => [time, model.sample(id, time)]))]));
        });
        assert.equal(states.B[4.9].owner, "neutral");
        assert.equal(states.B[5].owner, "blue");
        assert.equal(states.B[6].blue, 5);
        assert.equal(states.B[11].owner, "blue");
        assert(Math.abs(states.B[11].blue - 1) < 1e-6);
        assert.equal(states.B[12].owner, "neutral");
        assert.equal(states.B[16].owner, "neutral");
        assert(Math.abs(states.B[16].red - 4) < 1e-6);
        assert.equal(states.B[17].owner, "red");
        assert.equal(states.B[18].red, 5);
        assert(Math.abs(states.B[21].red - 3) < 1e-6);
        assert.equal(states.B[24].owner, "neutral");
        assert.equal(states.C[4.9].owner, "blue");
        assert.equal(states.C[5].owner, "neutral");
        assert.equal(states.C[9.9].owner, "neutral");
        assert.equal(states.C[10].owner, "red");
        assert.equal(states.C[14.9].owner, "red");
        assert.equal(states.C[15].owner, "neutral");
        assert.equal(states.C[20].owner, "blue");
        assert.equal(states.R[4.9].owner, "red");
        assert.equal(states.R[5].owner, "neutral");
        assert.equal(states.R[10].owner, "blue");
        assert.equal(states.R[20].owner, "red");
        const partial = await page.evaluate(() => window.__controlTransition.advance({ blue: 2, red: 0, owner: "neutral" }, "none", 1));
        assert.equal(partial.blue, 2);
        const contested = await page.evaluate(() => window.__controlTransition.advance({ blue: 3, red: 0, owner: "blue" }, "both", 1));
        assert.equal(contested.blue, 3); assert.equal(contested.owner, "blue");
        await fs.mkdir(frame_dir, { recursive: true });
        const keyframes = [];
        for (let index = 0; index <= 120; index++) {
            const time = index / 5;
            await page.evaluate(time => window.__controlTransition.seek(time), time);
            // 进度与内填必须裁切在相同原始轮廓上，字母不带任何transform。
            const geometry = await page.locator(".point").evaluateAll(nodes => nodes.map(svg => {
                const path = selector => svg.querySelector(selector).getAttribute("d");
                return { owner: svg.dataset.owner, shape: path(".existing-outline"),
                    shared_shape: path(".existing-outline") === path(".interior") && path(".interior") === path(".existing-progress"),
                    path_count: svg.querySelectorAll(".existing-outline,.existing-progress").length,
                    fixed_letter: getComputedStyle(svg.querySelector(".letter")).transform === "none" };
            }));
            geometry.forEach(item => assert(item.shared_shape && item.path_count === 2 && item.fixed_letter));
            if ([0, 12, 24, 30, 40, 50, 60, 80, 85, 100, 120].includes(index)) keyframes.push({ time, geometry });
            await page.locator(".field").screenshot({ path: path.join(frame_dir, `${String(index).padStart(3, "0")}.png`) });
        }
        await page.evaluate(() => window.__controlTransition.seek(0));
        await page.locator(".play").click();
        assert.equal(await page.locator(".play").textContent(), "Pause");
        await page.locator(".play").click();
        assert.equal(await page.locator(".play").textContent(), "Play");
        await page.locator(".seek").fill("12");
        assert((await page.locator("output").textContent()).includes("00:12"));
        await page.emulateMedia({ reducedMotion: "reduce" });
        await page.locator(".replay").click();
        assert.equal(await page.locator(".play").textContent(), "Pause");
        await page.locator(".play").click();
        await page.setViewportSize({ width: 320, height: 500 });
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        assert.deepEqual(errors, []); assert.deepEqual(requests, []);
        // 把已绘制首帧写回静态标记，无脚本时也能看到三个点，而不是空白区域。
        await page.evaluate(() => window.__controlTransition.seek(0));
        const initial = await page.locator(".points").innerHTML();
        // 使用精确DOM边界替换，避免正则跨越field容器造成嵌套错误。
        const start = source.indexOf('        <div class="points">') + '        <div class="points">'.length;
        const end = source.indexOf("\n        </div>", start);
        await fs.writeFile(path.join(root, "control_transition.html"), source.slice(0, start) + initial + source.slice(end));
        const no_script = await browser.newPage({ javaScriptEnabled: false });
        await no_script.setContent(source.slice(0, start) + initial + source.slice(end));
        assert.equal(await no_script.locator(".existing-outline").count(), 3);
        assert.equal(await no_script.locator(".letter").count(), 3);
        await no_script.close();
        await fs.writeFile(path.join(root, "control_transition_validation.json"), JSON.stringify({ states, keyframes, errors, requests,
            checks: ["all six owner transitions", "old-owner shape until zero", "neutral square until five", "contested pause", "two-second delayed deactivation fixture", "unoccupied controlled decay", "neutral partial progress does not auto-decay", "same outline for fill and stroke", "upright letters", "play/pause/replay/seek", "320px fit", "no network"],
            limit: "Rule-derived demonstration; no telemetry or verified per-point progress transport. Fractional values are presentation interpolation at 1 point per second." }, null, 4) + "\n");
        console.log("PASS: rules, all transitions, previous-owner shape, synchronized fill/stroke, playback and 320px.");
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
