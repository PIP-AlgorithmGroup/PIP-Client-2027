# 自定义终端 HUD 设计档案

当前设计基线更新至2026-11-23，采用Silver银灰竖向RMUL小地图；本人顶部卡片放大20%并保留渐变描边；顶部顺序为己方7/4/3/1、敌方1/3/4/7，敌方建筑下方显示经济行，脱战信息位于自身血条右侧。状态为独立视觉评审稿，产品实现范围以项目验收记录为准。
本档案保存当前设计及其依据，不代表用户已逐项验收，也不把展示数值定义为比赛规则。
后续修改已按顺序补录到[设计日志](design_log.md)，包含左右布局、被替代方案、Silver地图、战地式顶部、占领动画及规则核对结论。
本人卡片按己方身份匹配，默认突出3号步兵。当前20%放大替代早期5.5%方案，双方建筑区同步下移24px。
2026-11-21更新：底部模块前的自绘指示条已换为官方PNG状态灯，在线绿灯，离线和无数据源统一红灯；中文名称保留。
状态灯对照：[在线绿灯](captures/module_lamps_online.png)、[离线或无数据源红灯](captures/module_lamps_no_data.png)。
底部最新排列为血量、超电长条、缓冲/功率、模块状态；文字与条形的扩散暗影已补齐，保持整体透明布局。
2026-11-21阴影对比结论：用户选择A（当前效果）作为基线；C的“白色扩散叠加柔和暗影”思路和实验参数保留在[设计规则第10节](design.md#10-进度条刻度与对比处理)，作为备选，不应用到当前源稿。
2026-11-22小地图定稿：[Silver设计规范](minimap_design.md)。地图内容320×480，2px灰边框、左/下24px留白；经济信息在地图上方20px等宽对齐。己下敌上；有信号只画己方哨兵，
无信号隐藏哨兵本体、圆底、方向箭头与交火效果，静态地形和固定场地符号保留。
查看[有信号完整HUD](captures/default_2560x1440.png)、[仅地图无信号完整HUD](captures/map_signal_off_2560x1440.png)、
[完整无数据HUD](captures/data_empty_2560x1440.png)，以及[有信号地图](captures/minimap_signal.png)、[无信号地图](captures/minimap_no_signal.png)。

2026-11-23顶部改为战地参考布局：两侧卡片156×204、间距32；中央整块横条夹计时，数字嵌在条内靠中央的一端。
下排恢复B/C/R地点字母：蓝方控制用圆圈、红方控制用菱形、中立用灰白正方形；未知用虚线方框，无点名或状态汉字。中央整组下移12px，点排与横条间隔28px。
详细布局、来源和无数据语义见[设计规则第5节](design.md#5-时间占领与事件)。
占领过渡已补充[规则核对与动画规范](control_transition_design.md)和[独立可播放演示](control_transition.html)：
旧归属进度到0前保留原形状，中立积累到5前保留方框，原外框与内部淡色底同步扫掠。
查看[动画](captures/control_transition.gif)与[关键帧](captures/control_transition_keyframes.png)；当前未并入完整HUD静态基线。
完整规则整理见[RMUL控制区与占领机制](rmul_control_rules.md)，涵盖激活、占领计数、归属、控制度、补偿、增益和胜负，以及明文未覆盖边界。

正常数据参考：[完整2K画面](captures/default_2560x1440.png)、[顶部卡片细节](captures/top_detail.png)。
当前默认为[完整无数据方案](no_data_design.md)，离线预览显示“等待比赛数据”。
查看[无数据2K截图](captures/data_empty_2560x1440.png)、[局部缺失](captures/data_partial_2560x1440.png)和[更新中断](captures/data_stale_2560x1440.png)。

## 从这里接手

1. 完整阅读[设计规则与交接说明](design.md)，了解用户要求、当前布局、状态及尚待明确的内容。
   涉及数据缺失、图标未知、字段过期或不适用时，一并读取[无数据设计](no_data_design.md)。
   查看[设计日志](design_log.md)，确认后续修改顺序、最终决定和独立实验的集成边界，避免恢复已否决方案。
2. 用浏览器打开[完整离线预览](index.html)，对照[默认无数据2K基线](captures/data_empty_2560x1440.png)；核对正常样式时查看[有效数据参考](captures/default_2560x1440.png)。
3. 调整具体参数时读取[可读样式快照](style_snapshot.css)和[布局测量](layout_snapshot.json)。
4. 实际编辑[唯一视觉源稿](preview_fragment.html)，然后更新预览、截图及验证记录。不要在包装页维护另一套设计。

对新的 Agent，可以直接说：

> 先读 docs/design/custom_terminal/readme.md 和 design.md，再查看当前 2K 截图与离线预览；在这版基线上继续修改。

## 保存内容

| 文件或目录 | 用途 |
| --- | --- |
| `design.md` | 用户已明确的要求、当前取值、状态与交接验收条件 |
| `design_log.md` | 会话后续修改的顺序、替代关系、最终决定及当前落地范围 |
| `no_data_design.md` | 各HUD模块的未知、局部缺失、过期、不适用与状态图标规则 |
| `minimap_design.md` | 已选Silver色板、竖向地形/高差/图标方向、地图信号与素材来源 |
| `control_transition_design.md`、`control_transition.html` | 联盟赛占领规则核对、六种归属方向与暂停/衰减动画实验 |
| `rmul_control_rules.md` | RMUL 2027 3V3控制区完整规则、示例、原文页码及UI数据边界 |
| `render_control_transition.cjs`、`control_transition_validation.json` | 独立动画的可复现采样与规则边界验证 |
| `preview_fragment.html` | 最新可编辑 HTML/CSS/JS 原稿，素材内嵌，2560×1440 设计坐标 |
| `index.html` | 从源稿生成的离线浏览页；没有比赛视野之外的说明文字 |
| `style_snapshot.css` | 源稿全部样式的可读副本，字体路径展开；生成文件 |
| `layout_snapshot.json` | 2K 画布内所有布局元素的矩形与计算样式；生成文件 |
| `fixture.json` | 当前演示阵容、数值及状态；生成文件，不是协议定义 |
| `captures/` | 正常与无数据的2K/1080p、部分缺失、过期、不适用，以及红方、脱战倒计时、目标死亡和明亮背景对照 |
| `assets/`、`asset_manifest.json` | 实际使用的图标与字体原文件、来源和 SHA-256 |
| `assets/minimap/` | Silver红蓝源SVG及独立来源清单；不依赖会话目录 |
| `provenance.json` | 视觉源稿与官方原包版本、摘要、来源地址及项目素材入口 |
| `references/` | 原始PDF、用户截图（含战地占领布局参考）、来源清单及每张图的含义 |
| `validation.json` | 离线资源、状态交互、布局与检查结果，含验证环境与未验证范围 |
| `baseline_manifest.json` | 交付文件逐项哈希，用于判断后续是否发生漂移 |
| `build_preview.py`、`render_preview.cjs` | 不依赖会话路径的离线预览更新与截图复现工具 |

这份档案已纳入[项目资源索引](../../resource_index.md)，使用项目工作区的其他 Agent 可以自行找到。
会话目录、临时截图目录和聊天记忆都不是继续设计的必要依赖。

## 更新与复现

更新源稿后，在仓库根目录运行：

```sh
python docs/design/custom_terminal/build_preview.py
node docs/design/custom_terminal/render_preview.cjs
```

第一条命令只依赖 Python 标准库。第二条使用已有 Playwright 和浏览器，通过 `NODE_PATH` 提供
Playwright；`PREVIEW_BROWSER_PATH` 可指定浏览器可执行文件，或用 `PREVIEW_BROWSER` 指定已安装通道。
工具没有自动下载浏览器；没有指定时使用 Playwright 默认 Chromium。
截图复现只影响本目录，渲染使用固定展示夹具，不调用 MQTT、图传、裁判系统或控制设备。
浏览器直接打开时显示完整无数据场景；设计控件由会话宿主提供，可切换正常、局部缺失、未更新和不适用。
正常场景支持点击敌卡选择目标，无数据场景点击敌卡也不能冒充获得真实目标数据。
脱离宿主时，可通过源稿中的 `state` 调整初始值，或用渲染工具复现已归档状态，不依赖宿主控件。
`layout_snapshot.json`与`default_*.png`继续记录正常夹具，`offline_*.png`记录默认无数据场景；
新状态截图用`data_`前缀，均由同一个源稿生成。

修改设计后同步 `design.md` 的有关规则及 `design_log.md` 的决策记录；源稿变化时再更新生成快照和清单。保留旧基线时使用 Git 历史，
避免在同一目录存放多个含义不清的“最终版”。源稿确定像素细节，文档解释意图；若两者冲突，
先核对最新用户要求并明确记录差异，不能静默把错误实现升级成设计要求。

## 来源与边界

兵种图标来自项目已选定的[第五版图标](../../../resources/icons/robomaster_2027/readme.md)，
其官方依据与项目改编边界以原目录清单为准。等级、脱战图标及模块红绿状态灯直接提取自官方客户端
`RoboMasterClient 12.0.0.213_Student` 的 `resources.assets`，是原始栅格图，不是官方原生 SVG。
本档案已独立保存实际使用素材；约 882 MiB 的[完整客户端素材导出](../../references/robomaster/local_archives/client_assets_12_0_0_213/readme.md)
仍属于本机忽略目录，新克隆未必存在，来源及恢复地址见[大型原包说明](../../references/robomaster/local_archives/readme.md)。
`source_manifest.json` 保留原提取批次清单，可能列出本基线未采用的其它资源；实际包含文件以
`asset_manifest.json` 为准。截图、样式/布局快照及浏览包装按用户的完整本地保存要求纳入设计档案，
属于可审阅的基线证据，与产品构建生成物分开；每次变更须同步生成，避免多份基线漂移。

本轮保存不修改正式 QML、CMake、协议或产品资源列表。Windows Edge 的离线验证只说明视觉稿可重现；
Qt、Ubuntu、实机、DPI、通信字段与权限逻辑需要实现阶段另行验证。
