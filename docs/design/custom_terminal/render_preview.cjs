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
    addToggle(state) { window.__designState = state; }
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
    for (const name of ["own_units", "opponent_units", "state", "control_points"]) {
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
            await page.evaluate(() => { window.__designState.dataScenario = "live"; window.__designRender(); });
            await page.evaluate(async () => {
                await document.fonts.load('500 24px "Redraw Numbers"');
                await document.fonts.load('700 36px "Redraw Numbers"');
                await document.fonts.ready;
                await Promise.all([...document.images].map(image => image.decode()));
                await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
            });
        }

        /** 把验证结论写入清单；任何失败都导致非零退出，不把问题静默记为通过。 */
        function check(name, passed, detail = null) {
            checks.push({ name, passed, detail });
            if (!passed) issues.push(name);
        }

        /** 比较可见内容、素材及几何，避免扩散阴影的亚像素栅格抖动被误判为状态恢复失败。 */
        async function visible_snapshot(context = page) {
            return context.evaluate(() => ({
                text: document.querySelector("#terminal-redraw").innerText,
                fields: [...document.querySelectorAll(".robot, .target, .dashboard-track, .dashboard-hp, .heat-meter, .module-status img, .self-rank")].map(element => {
                    const rect = element.getBoundingClientRect();
                    return { class: element.getAttribute("class"), src: element.getAttribute("src"),
                        style: element.getAttribute("style"), hidden: element.hidden,
                        rect: [rect.x, rect.y, rect.width, rect.height].map(value => Number(value.toFixed(3))) };
                }),
            }));
        }

        await reset();
        let empty_snapshot;
        let empty_symbol_bounds;
        const initial_visible = await visible_snapshot();
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
                enemy_building: (() => {
                    // 用有血量的基地实际右边缘验证，避免浏览器将flex-end序列化为flow-end的版本差异。
                    const track = document.querySelectorAll(".opponent-buildings .building-track")[1];
                    return Math.abs(track.getBoundingClientRect().right - track.querySelector("i").getBoundingClientRect().right) < .1;
                })(),
                canvas_overflow: document.documentElement.scrollHeight > 1440 || document.documentElement.scrollWidth > 2560,
            };
        });
        check("2K画布与占位均为2560×1440", baseline.canvas.width === 2560 && baseline.canvas.height === 1440 && baseline.root.height === 1440, baseline.canvas);
        check("底部没有额外画布空白", !baseline.canvas_overflow);
        check("所有图标离线可解码", baseline.images_loaded);
        check("在线模块均使用官方绿灯", await page.locator('.module-lamp[data-lamp="green"]').count() === 10);
        const conquest_layout = await page.evaluate(() => {
            const center = document.querySelector(".match-center").getBoundingClientRect();
            const cards = [...document.querySelectorAll(".robot")].map(node => node.getBoundingClientRect());
            const clock = document.querySelector(".time-value").getBoundingClientRect();
            const points = document.querySelector(".control-points")?.getBoundingClientRect();
            return { center_width: center.width, card_width: cards[0].width, card_height: cards[0].height,
                separated: cards.every(card => card.right <= center.left || card.left >= center.right),
                points_below: !!points && points.top > clock.bottom,
                point_ids: [...document.querySelectorAll(".control-point")].map(node => node.dataset.point) };
        });
        check("顶部卡片略缩且给战地式中央占领指示留空间", conquest_layout.card_width === 156 && conquest_layout.card_height === 204 && conquest_layout.center_width >= 700 && conquest_layout.separated && conquest_layout.points_below, conquest_layout);
        check("蓝方占领点按己方高地、中央、敌方高地排列", JSON.stringify(conquest_layout.point_ids) === JSON.stringify(["B", "C", "R"]));
        check("控制点恢复地点字母BCR，无旧图案和中文点位说明", await page.locator(".control-glyph").count() === 0 && (await page.locator(".control-points").innerText()).replace(/\s/g, "") === "BCR" && (await page.locator(".conquest-scoreboard").innerText()).replace(/\s/g, "") === "5004:58100");
        const point_spacing = await page.evaluate(() => {
            const bar = document.querySelector(".conquest-scoreboard").getBoundingClientRect();
            const points = document.querySelector(".control-points").getBoundingClientRect();
            return { bar_top: bar.top, gap: points.top - bar.bottom };
        });
        check("占领条整组下移12px且控制点与条留28px间隔", Math.abs(point_spacing.bar_top - 43.3333) < .1 && point_spacing.gap === 28, point_spacing);
        const integrated_bars = await page.evaluate(() => [...document.querySelectorAll(".team-occupation")].map(node => {
            const bar = node.querySelector(".occupation-track").getBoundingClientRect();
            const value = node.querySelector("b").getBoundingClientRect();
            const fill = node.querySelector("i").getBoundingClientRect();
            return { height: bar.height, inside: value.top >= bar.top && value.bottom <= bar.bottom && value.left >= bar.left && value.right <= bar.right,
                inner_anchor: node.classList.contains("own-occupation") ? Math.abs(fill.right - bar.right) < .1 : Math.abs(fill.left - bar.left) < .1 };
        }));
        check("整块42px占领横条包住数字且高亮从靠中央一端填充", integrated_bars.every(bar => bar.height === 42 && bar.inside && bar.inner_anchor), integrated_bars);
        const bottom_resources = await page.evaluate(() => {
            const capacitor = document.querySelector(".capacitor-track").getBoundingClientRect();
            const power = document.querySelector(".power-track").getBoundingClientRect();
            return { capacitor_above_power: capacitor.y < power.y,
                capacitor_wider: capacitor.width > power.width,
                capacitor_value: document.querySelector(".capacitor-value").textContent,
                power_value: document.querySelector(".power-value").textContent,
                text_shadow: getComputedStyle(document.querySelector(".hud-screen")).textShadow,
                hp_shadow: getComputedStyle(document.querySelector(".dashboard-hp-shadow")).filter };
        });
        check("超电居上方长条，功率移到缓冲右侧，数值单位随项目移动", bottom_resources.capacitor_above_power && bottom_resources.capacitor_wider && bottom_resources.capacitor_value.includes("62") && bottom_resources.capacitor_value.includes("%") && bottom_resources.power_value.includes("80") && bottom_resources.power_value.includes("W"), bottom_resources);
        check("文字扩散阴影及斜切血条轮廓阴影存在", bottom_resources.text_shadow !== "none" && bottom_resources.hp_shadow.includes("drop-shadow"));
        check("顶部存活卡片填充从整卡顶端开始", baseline.active_cards.every(card => Math.abs(card.card_y - card.fill_y) < 0.1));
        check("顶部阵亡卡片没有血量填充", baseline.dead_fill_count === 0);
        check("敌方建筑右端零点", baseline.enemy_building);
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
        const map_baseline = await page.evaluate(() => {
            const map = document.querySelector(".tactical-map").getBoundingClientRect();
            const economy = document.querySelector(".economy").getBoundingClientRect();
            return { x: map.x, bottom: map.bottom, width: map.width, height: map.height,
                economy_clear: Math.abs(map.top - economy.bottom - 20) < .1 && economy.left === map.left && economy.width === map.width,
                border: getComputedStyle(document.querySelector(".tactical-map")).borderTopWidth,
                glyph: document.querySelector(".map-unit [data-sentry-glyph]").getAttribute("href"),
                terrain: document.querySelector(".map-scene").innerHTML };
        });
        check("Silver地图保留2px边框及24px留白，经济对齐且间隔20px", map_baseline.x === 24 && map_baseline.bottom === 1416 && map_baseline.width === 324 && map_baseline.height === 484 && map_baseline.border === "2px" && map_baseline.economy_clear, { ...map_baseline, glyph: undefined, terrain: undefined });
        check("有信号只画己方哨兵和5个正立场地符号", await page.locator('.map-units [data-robot="blue-sentry"]').count() === 1 && await page.locator(".map-units [data-robot]").count() === 1 && await page.locator(".map-scene [data-field-symbol]").count() === 5 && await page.locator(".map-scene [data-field-symbol][transform]").count() === 0);
        const normal_animations = await page.evaluate(() => window.__designMapFire().length);
        check("地图有信号可触发有限交火环与方向短线", normal_animations === 2);
        await page.evaluate(() => { window.__designState.mapSignal = false; window.__designRender(); });
        check("地图单独无信号只隐藏完整哨兵组，地形和其它遥测保持", await page.locator(".map-units [data-robot], .map-units .fire-ring, .map-units [data-gimbal]").count() === 0 && await page.locator(".map-scene").innerHTML() === map_baseline.terrain && JSON.stringify(await visible_snapshot()) === JSON.stringify(initial_visible));
        check("地图无信号不能产生交火动画，亦不覆盖静态地图提示文字", await page.evaluate(() => window.__designMapFire().length) === 0 && await page.locator(".map-data-note").isHidden());
        await page.screenshot({ path: path.join(capture_dir, "map_signal_off_2560x1440.png") });
        await page.evaluate(() => { window.__designState.mapSignal = true; window.__designRender(); });
        await page.emulateMedia({ reducedMotion: "reduce" });
        check("地图减少动态效果仅保留单个淡出环", await page.evaluate(() => window.__designMapFire().length) === 1);
        await page.emulateMedia({ reducedMotion: "no-preference" });
        await page.evaluate(() => window.__designRender());
        // 从唯一HUD源稿采集两个地图视角，导出有/无信号全图，不维护第二套几何或状态实现。
        const map_frames = {};
        for (const signal of [true, false]) {
            map_frames[signal] = [];
            for (const color of ["blue", "red"]) {
                await page.evaluate(({ color, signal }) => {
                    Object.assign(window.__designState, { ownColor: color, mapSignal: signal });
                    window.__designRender();
                }, { color, signal });
                map_frames[signal].push(await page.locator(".tactical-map").evaluate(node => node.outerHTML));
                if (signal) check(`${color}方地图只显示同一正立哨兵轮廓`, await page.locator(`.map-units [data-robot="${color}-sentry"]`).count() === 1 && await page.locator(".map-unit [data-sentry-glyph]").getAttribute("href") === map_baseline.glyph);
            }
        }
        const map_page = await browser.newPage({ viewport: { width: 680, height: 530 }, deviceScaleFactor: 2 });
        const map_style = source.match(/<style>([\s\S]*?)<\/style>/)[1];
        for (const signal of [true, false]) {
            await map_page.setContent(`<html><style>html,body{margin:0;background:#1e1e1e}${map_style}
                #terminal-redraw{width:664px;aspect-ratio:auto;margin:0;padding:0}
                #terminal-redraw .map-pair{display:flex;gap:16px}
                #terminal-redraw figure{margin:0}
                #terminal-redraw figcaption{color:#ddd;text-align:center;font:14px Arial;padding:8px}
                #terminal-redraw .tactical-map{position:relative;left:auto;bottom:auto;box-shadow:none}
                </style><div id="terminal-redraw"><div class="map-pair">${map_frames[signal].map((frame, index) => `<figure><figcaption>${index ? "RED" : "BLUE"}</figcaption>${frame}</figure>`).join("")}</div></div></html>`);
            await map_page.locator(".map-pair").screenshot({ path: path.join(capture_dir, signal ? "minimap_signal.png" : "minimap_no_signal.png") });
        }
        await map_page.close();
        await page.evaluate(() => { Object.assign(window.__designState, { ownColor: "blue", mapSignal: true }); window.__designRender(); });
        for (const [name, selector] of [["top_detail", ".match-top"], ["bottom_detail", ".robot-dashboard"], ["target_detail", ".target"], ["module_lamps_online", ".module-status"]]) {
            if (name === "bottom_detail") {
                await page.screenshot({ path: path.join(capture_dir, name + ".png"), clip: { x: 680, y: 1170, width: 1440, height: 270 } });
            } else if (name === "top_detail") {
                // 本人卡片放大20%，局部截图保留条外渐变描边和下沿，不按原始网格高度裁切。
                await page.screenshot({ path: path.join(capture_dir, name + ".png"), clip: { x: 30, y: 0, width: 2500, height: 287 } });
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
            const point_order = await page.locator(".control-point").evaluateAll(nodes => nodes.map(node => node.dataset.point));
            check(`${color}方控制区身份与己下敌上顺序保持一致`, JSON.stringify(point_order) === JSON.stringify(color === "blue" ? ["B", "C", "R"] : ["R", "C", "B"]) && await page.locator('.control-point[data-owner="own"]').count() === 1 && await page.locator('.control-point[data-owner="opponent"]').count() === 1);
            const point_shapes = await page.locator(".control-point").evaluateAll(nodes => nodes.map(node => ({
                team: node.dataset.team, radius: getComputedStyle(node.querySelector(".control-symbol")).borderRadius,
                transform: getComputedStyle(node.querySelector(".control-symbol")).transform,
                letter: node.querySelector(".control-letter").textContent,
            })));
            check(`${color}方蓝圈红菱中立方框按真实归属，字母保持地点身份`, point_shapes.every(point => point.team === "blue" ? point.radius === "50%" && point.transform === "none" : point.radius === "0px" && (point.team === "red" ? point.transform !== "none" : point.transform === "none")) && point_shapes.map(point => point.letter).join("") === (color === "blue" ? "BCR" : "RCB"), point_shapes);
            const self_marker = await page.evaluate(() => {
                const cards = [...document.querySelectorAll(".robot.is-self")];
                const own_cards = [...document.querySelectorAll(".roster.own .robot")];
                const self = cards[0];
                const box = self?.getBoundingClientRect();
                const neighbours = own_cards.filter(card => card !== self).map(card => card.getBoundingClientRect());
                return { count: cards.length, kind: self?.dataset.kind,
                    on_own_side: !!self?.closest(".roster.own"),
                    enlarged: Math.abs(box?.width / neighbours[0]?.width - 1.2) < 0.001,
                    overlaps_neighbour: neighbours.some(peer => box.left < peer.right && box.right > peer.left),
                    clear_of_buildings: box.bottom + 5 < document.querySelector(".own-buildings").getBoundingClientRect().top,
                    inside_canvas: box.top >= 5 && box.left >= 5 && box.right <= 2555,
                };
            });
            check(`${color}方本人卡片放大20%且不遮邻卡或建筑`, self_marker.count === 1 && self_marker.kind === "infantry_3" && self_marker.on_own_side && self_marker.enlarged && !self_marker.overlaps_neighbour && self_marker.clear_of_buildings && self_marker.inside_canvas, self_marker);
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

        await reset();
        for (const scenario of ["empty", "partial", "stale", "unavailable"]) {
            await page.evaluate(scenario => {
                window.__designState.dataScenario = scenario;
                window.__designRender();
            }, scenario);
            await page.screenshot({ path: path.join(capture_dir, `data_${scenario}_2560x1440.png`) });
            if (scenario === "empty") {
                empty_snapshot = await visible_snapshot();
                empty_symbol_bounds = await page.locator(".robot-symbol img").evaluateAll(nodes => nodes.map(node => {
                    const box = node.getBoundingClientRect();
                    return { left: box.left - 32, top: box.top - 32, right: box.right + 32, bottom: box.bottom + 32 };
                }));
        const empty = await page.evaluate(() => ({
                    notice: document.querySelector(".data-notice").textContent,
                    health: [...document.querySelectorAll(".robot-health")].map(value => value.textContent),
                    dead: document.querySelectorAll(".robot.is-dead").length,
                    fills: document.querySelectorAll(".robot-fill").length,
                    positions: document.querySelectorAll(".map-unit").length,
                    rank_hidden: document.querySelector(".self-rank").hidden,
                    combat_hidden: document.querySelector(".combat-status img").hidden,
                    module_unknown: document.querySelectorAll(".module-status > span.is-missing").length,
                    heat_unknown: document.querySelector(".heat-meter").classList.contains("is-missing"),
                    target: document.querySelector(".target-name").textContent,
                    self_count: document.querySelectorAll(".robot.is-self").length,
                }));
                check("无数据不冒充0血量或阵亡，全部未知且保留本人标记", empty.fills === 0 && empty.dead === 0 && empty.health.every(text => text === "— / —") && empty.self_count === 1, empty);
                check("无数据不显示虚构位置、等级、脱战结论或正常模块", empty.positions === 0 && empty.rank_hidden && empty.combat_hidden && empty.module_unknown === 10 && empty.heat_unknown && empty.target === "目标未知");
                check("无数据模块统一官方红灯且无旧问号", await page.locator('.module-lamp[data-lamp="red"]').count() === 10 && !(await page.locator(".module-status").textContent()).includes("?"));
                check("无数据两条占领条无有效填充，三个控制区未知而非未占领", await page.locator(".occupation-track.is-missing").count() === 2 && await page.locator('.control-point[data-owner="unknown"][data-quality="missing"]').count() === 3 && (await page.locator(".control-point").evaluateAll(nodes => nodes.map(node => node.getAttribute("aria-label")))).every(text => text.includes("状态未知")));
                await page.locator(".module-status").screenshot({ path: path.join(capture_dir, "module_lamps_no_data.png") });
                await page.setViewportSize({ width: 1920, height: 1080 });
                await page.screenshot({ path: path.join(capture_dir, "data_empty_1920x1080.png") });
                await page.setViewportSize({ width: 2560, height: 1440 });
                await page.screenshot({ path: path.join(capture_dir, "data_empty_bottom.png"), clip: { x: 680, y: 1170, width: 1440, height: 270 } });
            } else if (scenario === "partial") {
                const partial = await page.evaluate(() => ({
                    self_hp: document.querySelector(".dashboard-hp > b").textContent,
                    capacitor: document.querySelector(".capacitor-value").textContent,
                    own_fills: document.querySelectorAll(".own .robot-fill").length,
                    enemy_fills: document.querySelectorAll(".opponent .robot-fill").length,
                }));
                check("部分缺失只影响对应字段，自己HP与队友数据保留", partial.self_hp.includes("320") && partial.capacitor === "— %" && partial.own_fills === 4 && partial.enemy_fills === 0, partial);
            } else if (scenario === "stale") {
                check("过期保留最后数值，计时不推进，目标不冒充锁定", (await page.locator(".dashboard-hp > b").textContent()).includes("320") && (await page.locator(".data-notice").textContent()).includes("3.2 s") && (await page.locator(".target-name").textContent()) === "目标未知");
                check("地图位置过期时隐藏哨兵全部动态几何", await page.locator(".map-units [data-robot], .map-units [data-gimbal], .map-units .fire-ring").count() === 0);
                check("控制区过期保留归属但明确未更新", await page.locator('.control-point[data-quality="stale"]').count() === 3 && (await page.locator(".control-point").evaluateAll(nodes => nodes.map(node => node.getAttribute("aria-label")))).every(text => text.includes("未更新")));
            } else {
                check("不适用与未锁定均显示明确语义", (await page.locator(".capacitor-value").textContent()) === "N/A" && (await page.locator(".target-name").textContent()) === "未锁定目标");
            }
        }
        await page.evaluate(() => {
            Object.assign(window.__designState, { dataScenario: "live", fieldQuality: { heat: "invalid", power: "missing" } });
            window.__designRender();
        });
        check("字段级非法和缺失可分别标记且不会污染其它字段", (await page.locator(".heat-reading b").getAttribute("data-quality")) === "invalid" && (await page.locator(".power-row > b").getAttribute("data-quality")) === "missing" && (await page.locator(".dashboard-hp > b").textContent()).includes("320"));
        await page.evaluate(() => {
            window.__designState.fieldQuality = { selfHp: "missing" };
            window.__designRender();
        });
        check("同一自身HP缺失同步顶部本人卡片与底部，等级弹量仍有效", (await page.locator(".robot.is-self .robot-health").textContent()) === "— / —" && (await page.locator(".dashboard-hp > b").textContent()) === "— / —" && await page.locator(".robot.is-self .robot-fill").count() === 0 && (await page.locator(".robot.is-self .robot-level").textContent()) === "LV.4");
        await page.evaluate(() => {
            Object.assign(window.__designState, { dataScenario: "live", fieldQuality: {}, heat: 0 });
            window.__designRender();
        });
        check("真实0热量仍为有效值，与未知横杠区分", (await page.locator(".heat-reading b").textContent()) === "0" && !(await page.locator(".heat-meter").getAttribute("class")).includes("is-missing"));
        await page.evaluate(() => {
            window.__designState.heat = 19;
            window.__designRender();
        });
        await page.evaluate(async () => { await Promise.all([...document.images].map(image => image.decode())); });
        await page.screenshot({ path: path.join(capture_dir, "restored_live_2560x1440.png") });
        check("所有缺失状态往返后恢复原有效内容、图标和布局", JSON.stringify(await visible_snapshot()) === JSON.stringify(initial_visible));

        // 离线包装也必须工作；视觉与文字应和源稿默认状态一致。
        await page.goto(pathToFileURL(path.join(root, "index.html")).href);
        const frame = page.frameLocator("iframe");
        await frame.locator(".combat-status img").evaluate(image => image.decode());
        check("独立离线浏览页默认显示完整无数据状态", (await frame.locator(".combat-status").textContent()).trim() === "脱战未知" && (await frame.locator(".data-notice").textContent()) === "等待比赛数据");
        const exported = await frame.locator("#terminal-redraw").boundingBox();
        check("离线包装保留2K画布比例", exported.width === 2560 && exported.height === 1440, exported);
        await frame.locator(".hud-screen").evaluate(async () => {
            await document.fonts.ready;
            await Promise.all([...document.images].map(image => image.decode()));
        });
        await frame.locator(".hud-screen").screenshot({ path: path.join(capture_dir, "offline_2560x1440.png") });
        const original_image = await fs.readFile(path.join(capture_dir, "data_empty_2560x1440.png"));
        const offline_image = await fs.readFile(path.join(capture_dir, "offline_2560x1440.png"));
        // 透明图标的多层drop-shadow在顶层和iframe合成时会有局部栅格舍入。
        // 仍要求全部文字/素材/几何一致，且像素差只能出现在这些图标及阴影范围内。
        const wrapper_snapshot = await visible_snapshot(page.frames().find(candidate => candidate !== page.mainFrame()));
        const pixels = await page.evaluate(async ({ first, second, bounds }) => {
            const read = async base64 => {
                const image = new Image(); image.src = `data:image/png;base64,${base64}`; await image.decode();
                const canvas = document.createElement("canvas"); canvas.width = image.width; canvas.height = image.height;
                const context = canvas.getContext("2d"); context.drawImage(image, 0, 0);
                return { width: image.width, height: image.height, pixels: context.getImageData(0, 0, image.width, image.height).data };
            };
            const a = await read(first), b = await read(second);
            let changed = 0, maximum = 0, outside = 0;
            if (a.width !== b.width || a.height !== b.height) return { same_size: false };
            for (let offset = 0; offset < a.pixels.length; offset += 4) {
                const difference = Math.max(...[0, 1, 2, 3].map(channel => Math.abs(a.pixels[offset + channel] - b.pixels[offset + channel])));
                if (!difference) continue;
                changed++; maximum = Math.max(maximum, difference);
                const x = offset / 4 % a.width, y = Math.floor(offset / 4 / a.width);
                if (!bounds.some(box => x >= box.left && x <= box.right && y >= box.top && y <= box.bottom)) outside++;
            }
            return { same_size: true, changed_fraction: changed / a.width / a.height, maximum, outside };
        }, { first: original_image.toString("base64"), second: offline_image.toString("base64"), bounds: empty_symbol_bounds });
        check("独立包装与源稿内容几何一致，仅允许图标阴影局部栅格差", JSON.stringify(wrapper_snapshot) === JSON.stringify(empty_snapshot) && pixels.same_size && pixels.outside === 0 && pixels.maximum <= 40 && pixels.changed_fraction < .0025, pixels);
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
    await save_json("baseline_manifest.json", { baseline: "2026-11-23", source_sha256: source_hash,
        status: "visual_review", files });
    console.log(JSON.stringify({ checks: checks.length, files: files.length, issues }, null, 4));
    if (issues.length) process.exitCode = 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
