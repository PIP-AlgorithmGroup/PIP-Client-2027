/**
 * 渲染独立 HTML 评审稿，不是客户端依赖。通过 NODE_PATH 提供 Playwright，
 * PREVIEW_BROWSER 可选指定浏览器；输出仅在本目录，浏览器资源在退出前释放。
 */
const { chromium } = require("playwright");
const fs = require("node:fs/promises");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

(async () => {
    const browser = await chromium.launch({
        channel: process.env.PREVIEW_BROWSER || "msedge",
        headless: true,
    });
    const issues = [];
    try {
        const page = await browser.newPage({ deviceScaleFactor: 1 });
        page.on("pageerror", error => issues.push(error.message));
        page.on("requestfailed", request => issues.push(request.url() + ": " + request.failure().errorText));
        const base = pathToFileURL(path.join(__dirname, "index.html")).href;
        const measurements = [];
        for (const [width, height] of [[1280,720],[1920,1080],[1024,640]]) {
            await page.setViewportSize({ width, height });
            for (const view of ["waiting", "session"]) {
                await page.goto(base + "?view=" + view + "&capture=1");
                await page.evaluate(() => document.fonts.ready);
                await page.evaluate(() => document.fonts.load('14px "Preview Sans"'));
                await page.screenshot({ path: path.join(__dirname, `${view}_${width}x${height}.png`) });
                const geometry = await page.evaluate(() => {
                    const menu = document.querySelector(".menu").getBoundingClientRect();
                    const content = document.getElementById("session-content");
                    return {
                        width: innerWidth,
                        height: innerHeight,
                        headlineSize: getComputedStyle(document.getElementById("signal-title")).fontSize,
                        menu: { x: menu.x, y: menu.y, width: menu.width, height: menu.height },
                        horizontalOverflow: content.scrollWidth > content.clientWidth,
                        bodyOverflow: document.documentElement.scrollWidth > innerWidth,
                        fontLoaded: document.fonts.check('14px "Preview Sans"'),
                        fonts: [...document.fonts].map(font => ({ family: font.family, status: font.status })),
                        bodyFont: getComputedStyle(document.body).fontFamily,
                    };
                });
                measurements.push({ view, ...geometry });
                if (geometry.bodyOverflow || geometry.horizontalOverflow || !geometry.fontLoaded) issues.push(`${width}x${height}/${view}: layout or font issue`);
            }
        }
        // 检查原型行为：两级身份、Esc、错误状态和 UI 档位，只影响浏览器内存。
        await page.goto(base + "?view=session");
        await page.selectOption("#team", "red");
        if (await page.locator("#robot-id").textContent() !== "—") issues.push("team change must clear robot");
        if (!await page.locator("#save").isDisabled()) issues.push("unselected identity must disable save");
        await page.selectOption("#robot", "3");
        if (await page.locator("#robot-id").textContent() !== "3") issues.push("red robot mapping");
        await page.selectOption("#team", "blue");
        await page.selectOption("#robot", "3");
        if (await page.locator("#robot-id").textContent() !== "103") issues.push("blue robot mapping");
        await page.keyboard.press("Escape");
        if (!await page.locator("#menu-layer").isHidden()) issues.push("Esc closes menu");
        await page.selectOption("#preview-state", "error");
        if (await page.locator("#signal-title").textContent() !== "图传暂不可用") issues.push("error state label");
        await page.selectOption("#preview-size", "1.12");
        await page.keyboard.press("Escape");
        const largeGeometry = await page.locator(".menu").boundingBox();
        if (largeGeometry.x < 0 || largeGeometry.y < 0) issues.push("large UI menu outside viewport");
        await fs.writeFile(path.join(__dirname, "validation.json"), JSON.stringify({ measurements, issues }, null, 4) + "\n");
        console.log(JSON.stringify({ captures: measurements.length, issues }, null, 2));
        if (issues.length) process.exitCode = 1;
    } finally {
        await browser.close();
    }
})();
