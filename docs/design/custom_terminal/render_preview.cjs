/**
 * 复现归档 HUD 的固定展示状态并保存截图、布局和完整性记录。
 * 仅处理本目录，不运行客户端或连接任何数据源；通过环境提供现有 Playwright 和浏览器。
 */
const { chromium } = require("playwright");
const fs = require("node:fs/promises");
const path = require("node:path");
const vm = require("node:vm");
const { createHash } = require("node:crypto");
const { pathToFileURL } = require("node:url");

const root = __dirname;

/** 仅替换可选的设计控件桥，暴露原稿已有展示状态，不修改原稿或增加产品控制。 */
const fixture_bridge = `<script>
window.Tweak = class {
    constructor(options) { window.__designRender = options.onChange; }
    addSelect(state) { window.__designState = state; }
    addSlider(state) { window.__designState = state; }
};
</script>`;

/** 递归遍历本档案，生成排序稳定的相对路径；不跨出本目录。 */
async function list_files(directory) {
    const files = [];
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
        const file = path.join(directory, entry.name);
        if (entry.isDirectory()) files.push(...await list_files(file));
        else if (entry.isFile()) files.push(file);
    }
    return files.sort();
}

/** 使用UTF-8与LF保存机器可读快照，不写入本机绝对路径或运行缓存。 */
async function save_json(name, value) {
    await fs.writeFile(path.join(root, name), JSON.stringify(value, null, 4) + "\n");
}

(async () => {
    const source = await fs.readFile(path.join(root, "preview_fragment.html"), "utf8");
    const source_hash = createHash("sha256").update(source).digest("hex");
    const provenance = JSON.parse(await fs.readFile(path.join(root, "provenance.json"), "utf8"));
    provenance.source_fragment_sha256 = source_hash;
    await save_json("provenance.json", provenance);
    const fixtures = {};
    // 只求值本仓库已知视觉源稿中的静态字面量，限制执行时间，不执行原包代码。
    for (const name of ["own_units", "opponent_units", "state"]) {
        const match = source.match(new RegExp(`const ${name} = ([\\s\\S]*?);\\n`));
        if (!match) throw new Error(`Missing authored fixture: ${name}`);
        fixtures[name] = vm.runInNewContext(`(${match[1]})`, {}, { timeout: 1000 });
    }
    await save_json("fixture.json", fixtures);
    const launch = { headless: true };
    if (process.env.PREVIEW_BROWSER_PATH) launch.executablePath = process.env.PREVIEW_BROWSER_PATH;
    else if (process.env.PREVIEW_BROWSER) launch.channel = process.env.PREVIEW_BROWSER;
    const browser = await chromium.launch(launch);
    const issues = [];
    const checks = [];
    const capture_dir = path.join(root, "captures");
    await fs.mkdir(capture_dir, { recursive: true });
    const assets = JSON.parse(await fs.readFile(path.join(root, "asset_manifest.json"), "utf8"));
    for (const asset of assets) {
        const actual = createHash("sha256").update(await fs.readFile(path.join(root, asset.path))).digest("hex");
        if (actual !== asset.sha256) issues.push("Asset hash mismatch: " + asset.path);
    }
    try {
        const page = await browser.newPage({ viewport: { width: 2560, height: 1440 }, deviceScaleFactor: 1 });
        page.on("pageerror", error => issues.push(error.message));
        page.on("requestfailed", request => issues.push(request.failure().errorText));
        page.on("request", request => {
            if (/^https?:/.test(request.url())) issues.push("Unexpected network dependency: " + request.url());
        });

        /** 每次从原稿重新加载；独立包装避免浏览器默认8px边距改变画布比例。 */
        async function reset() {
            await page.setViewportSize({ width: 2560, height: 1440 });
            await page.setContent(`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><style>html,body{margin:0;padding:0}</style>${fixture_bridge}${source}</html>`);
            await page.evaluate(async () => {
                await document.fonts.ready;
                await Promise.all([...document.images].map(image => image.decode()));
            });
        }

        /** 把验证结论写入清单；任何失败都导致非零退出，不把问题静默记为通过。 */
        function check(name, passed, detail = null) {
            checks.push({ name, passed, detail });
            if (!passed) issues.push(name);
        }

        await reset();
        const baseline = await page.evaluate(() => {
            const box = selector => {
                const value = document.querySelector(selector).getBoundingClientRect();
                return { x: value.x, y: value.y, width: value.width, height: value.height };
            };
            return {
                canvas: box(".hud-screen"), root: box("#terminal-redraw"), hp: box(".dashboard-hp"),
                combat: box(".combat-status"), text: document.querySelector(".combat-status").textContent,
                images_loaded: [...document.images].every(image => image.complete && image.naturalWidth > 0),
                own: box(".roster.own"), opponent: box(".roster.opponent"),
                dead_fill_count: document.querySelectorAll(".robot.is-dead .robot-fill").length,
                active_cards: [...document.querySelectorAll(".robot:not(.is-dead)")].map(card => {
                    const fill = card.querySelector(".robot-fill").getBoundingClientRect();
                    return { card_y: card.getBoundingClientRect().y, fill_y: fill.y };
                }),
                enemy_building: getComputedStyle(document.querySelector(".opponent-buildings .building-track")).justifyContent,
                canvas_overflow: document.documentElement.scrollHeight > 1440 || document.documentElement.scrollWidth > 2560,
            };
        });
        check("2K画布与占位均为2560×1440", baseline.canvas.width === 2560 && baseline.canvas.height === 1440 && baseline.root.height === 1440, baseline.canvas);
        check("底部没有额外画布空白", !baseline.canvas_overflow);
        check("所有图标离线可解码", baseline.images_loaded);
        check("顶部存活卡片填充从整卡顶端开始", baseline.active_cards.every(card => Math.abs(card.card_y - card.fill_y) < 0.1));
        check("顶部阵亡卡片没有血量填充", baseline.dead_fill_count === 0);
        check("敌方建筑右端零点", baseline.enemy_building === "flex-end");
        check("脱战信息在自身血条右侧并垂直居中", Math.abs(baseline.combat.x - baseline.hp.x - baseline.hp.width - 14) < 0.1 && Math.abs(baseline.combat.y + baseline.combat.height / 2 - baseline.hp.y - baseline.hp.height / 2) < 0.1);

        const layout = await page.evaluate(() => [...document.querySelectorAll("#terminal-redraw, #terminal-redraw *")].map(element => {
            const rect = element.getBoundingClientRect();
            const style = getComputedStyle(element);
            const properties = ["position", "display", "width", "height", "top", "right", "bottom", "left", "padding", "margin", "gap", "rowGap", "columnGap", "gridTemplateColumns", "gridTemplateRows", "alignItems", "justifyContent", "fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing", "color", "backgroundColor", "backgroundImage", "border", "boxShadow", "textShadow", "filter", "opacity", "clipPath", "transform", "zIndex", "overflow", "fill", "stroke", "strokeWidth", "strokeDasharray"];
            return {
                tag: element.tagName.toLowerCase(), id: element.id || null,
                class: element.getAttribute("class"),
                text: element.children.length === 0 && !["SCRIPT", "STYLE"].includes(element.tagName) ? element.textContent.trim() : null,
                rect: Object.fromEntries(["x", "y", "width", "height"].map(key => [key, Number(rect[key].toFixed(4))])),
                style: Object.fromEntries(properties.map(key => [key, style[key]])),
            };
        }));
        await save_json("layout_snapshot.json", { canvas: [2560, 1440], origin: "top-left", units: "design pixels", source_sha256: source_hash, elements: layout });
        await page.screenshot({ path: path.join(capture_dir, "default_2560x1440.png") });
        for (const [name, selector] of [["top_detail", ".match-top"], ["bottom_detail", ".robot-dashboard"], ["target_detail", ".target"]]) {
            if (name === "bottom_detail") {
                await page.screenshot({ path: path.join(capture_dir, name + ".png"), clip: { x: 680, y: 1170, width: 1440, height: 270 } });
            } else if (name === "top_detail") {
                // 本人卡片略微放大，局部截图保留条外渐变描边和下沿，不按原始网格高度裁切。
                await page.screenshot({ path: path.join(capture_dir, name + ".png"), clip: { x: 40, y: 3, width: 2480, height: 258 } });
            } else await page.locator(selector).screenshot({ path: path.join(capture_dir, name + ".png") });
        }

        for (const color of ["blue", "red"]) {
            for (let level = 1; level <= 10; level++) {
                await page.evaluate(({ color, level }) => {
                    Object.assign(window.__designState, { ownColor: color, selfLevel: level });
                    window.__designRender();
                }, { color, level });
                await page.locator(".self-rank").evaluate(image => image.decode());
            }
            const order = await page.evaluate(() => document.querySelector(".roster.own").getBoundingClientRect().left < document.querySelector(".roster.opponent").getBoundingClientRect().left);
            check(`${color}方等级1至10可加载且己方仍在左`, order);
            const self_marker = await page.evaluate(() => {
                const cards = [...document.querySelectorAll(".robot.is-self")];
                const own_cards = [...document.querySelectorAll(".roster.own .robot")];
                const self = cards[0];
                const box = self?.getBoundingClientRect();
                const neighbours = own_cards.filter(card => card !== self).map(card => card.getBoundingClientRect());
                return { count: cards.length, kind: self?.dataset.kind,
                    on_own_side: !!self?.closest(".roster.own"),
                    enlarged: box?.width > neighbours[0]?.width,
                    overlaps_neighbour: neighbours.some(peer => box.left < peer.right && box.right > peer.left),
                };
            });
            check(`${color}方仅突出本人3号步兵且放大后不遮邻卡`, self_marker.count === 1 && self_marker.kind === "infantry_3" && self_marker.on_own_side && self_marker.enlarged && !self_marker.overlaps_neighbour, self_marker);
        }
        await page.evaluate(() => { window.__designState.selfLevel = 4; window.__designRender(); });
        await page.screenshot({ path: path.join(capture_dir, "own_red_2560x1440.png") });

        await page.evaluate(() => {
            Object.assign(window.__designState, { combatStatus: "engaged", heat: 82 });
            window.__designRender();
        });
        await page.locator(".combat-status img").evaluate(image => image.decode());
        const heat = await page.locator(".heat-progress").evaluateAll(arcs => arcs.map(arc => ({ fill: arc.style.strokeDasharray, color: arc.style.stroke })));
        check("两侧圆弧共用82%热量和高热色", heat.length === 2 && heat[0].fill === heat[1].fill && heat[0].fill.startsWith("82") && heat[0].color === heat[1].color, heat);
        check("交战状态显示5s后脱战", (await page.locator(".combat-status").textContent()).trim() === "5s 后脱战");
        await page.screenshot({ path: path.join(capture_dir, "combat_countdown_2560x1440.png") });
        await page.locator('.opponent [data-kind="infantry_4"]').click();
        check("点击阵亡目标保留完整身份并显示阵亡", (await page.locator(".target-name").textContent()) === "4 号步兵" && (await page.locator(".target-level").textContent()) === "已阵亡");
        await page.screenshot({ path: path.join(capture_dir, "dead_target_2560x1440.png") });

        await reset();
        await page.setViewportSize({ width: 1920, height: 1080 });
        await page.screenshot({ path: path.join(capture_dir, "default_1920x1080.png") });
        await reset();
        await page.addStyleTag({ content: "#terminal-redraw .video-surface{background:#f6f6f6}#terminal-redraw .video-surface>svg{visibility:hidden}#terminal-redraw .video-surface::after{background:none}" });
        await page.screenshot({ path: path.join(capture_dir, "contrast_bright_2560x1440.png") });

        // 离线包装也必须工作；视觉与文字应和源稿默认状态一致。
        await page.goto(pathToFileURL(path.join(root, "index.html")).href);
        const frame = page.frameLocator("iframe");
        await frame.locator(".combat-status img").evaluate(image => image.decode());
        check("独立离线浏览页可显示最新脱战状态", (await frame.locator(".combat-status").textContent()).trim() === "已脱战");
        const exported = await frame.locator("#terminal-redraw").boundingBox();
        check("离线包装保留2K画布比例", exported.width === 2560 && exported.height === 1440, exported);
        await frame.locator(".hud-screen").evaluate(async () => {
            await document.fonts.ready;
            await Promise.all([...document.images].map(image => image.decode()));
        });
        await frame.locator(".hud-screen").screenshot({ path: path.join(capture_dir, "offline_2560x1440.png") });
        const original_image = await fs.readFile(path.join(capture_dir, "default_2560x1440.png"));
        const offline_image = await fs.readFile(path.join(capture_dir, "offline_2560x1440.png"));
        check("独立浏览包装与源稿截图逐字节一致", original_image.equals(offline_image));
    } finally {
        const validation = { source_sha256: source_hash, platform: process.platform,
            browser: browser.version(), device_scale_factor: 1,
            checks, issues, limitations: ["Windows离线原型检查；未执行Qt或Ubuntu验证。", "当前所有数值均为展示夹具；未连接协议、图传、自瞄或控制链路。", "中文使用本机字体，跨机器可能存在字形及文字宽度差异。"] };
        await save_json("validation.json", validation);
        await browser.close();
    }
    const files = [];
    for (const file of await list_files(root)) {
        const relative = path.relative(root, file).split(path.sep).join("/");
        if (relative === "baseline_manifest.json") continue;
        const bytes = await fs.readFile(file);
        files.push({ path: relative, bytes: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex") });
    }
    await save_json("baseline_manifest.json", { baseline: "2026-11-20", source_sha256: source_hash,
        status: "visual_review", files });
    console.log(JSON.stringify({ checks: checks.length, files: files.length, issues }, null, 4));
    if (issues.length) process.exitCode = 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
