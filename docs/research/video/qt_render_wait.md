# Qt 图传输出至渲染同步等待调研

日期：2026-11-13。研究基线为项目当前 Qt 6.9 / Qt 官方源码标签 `v6.9.0`。
本记录区分公开 API 契约、指定版本实现与本机实验，不将私有实现当作跨版本保证。
本轮只调研与测量，不改变生产渲染循环或环境变量默认值。
后续已完成独立 Qt 6.12.0 实际构建、回归、部署和版本 A/B，见[补充实测](../../validation/qt612.md)；
以下源码研究与 Qt 6.9 实验仍保留原有适用范围。

## 现有指标实际包含什么

当前 `VideoView::updatePaintNode()` 记录的 `GuiWait` 从同一帧 RGBA 输出完成的
`decoded_us` 开始，到该帧进入 `updatePaintNode()` 的 `began_us` 结束。
应用路径为：解码线程写最新帧邮箱 → 合并一次 GUI 队列调用 → `VideoReceiver::poll()`
发射 `frameChanged` → `VideoView::refresh()` 取得快照并调用 `QQuickItem::update()`
→ 窗口更新调度 → 场景图开始帧与同步 → `updatePaintNode()`。
它不是纯 GUI 消息排队时间，也不是 GPU 执行时间。

Qt 6.9 threaded render loop 在同步前先发射 `beforeFrameBegin()`，随后调用
`QRhi::beginFrame()`，成功后才进入同步及 `updatePaintNode()`。
D3D11 的 `beginFrame()` 内还可能等待 swapchain 的 frame-latency waitable object。
所以当前指标可以包含 GUI 调度、渲染线程调度、上一帧资源可用等待与同步前工作。
来源：[Qt threaded render loop 源码，602–716 行][S3]、[D3D11 beginFrame，1384–1395 行][S5]。

最有价值的下一组时间点是同一帧 `decoded_us`、GUI 取帧/请求更新、
`beforeFrameBegin()`、`beforeSynchronizing()`、`updatePaintNode()` 与 `frameSwapped()`。
其中窗口级回调必须与实际采样的帧版本关联，不能给多个帧借用同一个回调时间，
也不能将本轮未呈交的帧混入呈交延迟统计。

## Qt 官方调度事实

### 更新请求不是直接执行一次绘制

`QQuickItem::update()` 将项标记为需要更新；threaded loop 的更新请求经
`QWindow::requestUpdate()` 到平台窗口。Qt 6.9 的具体链路是
`maybeUpdate()` → `postUpdateRequest()` → `window->requestUpdate()`。
同一窗口的重复请求合并为一次；`QWindow::requestUpdate()` 必须从 GUI 主线程调用。
不能从解码线程直接调用 GUI 项的 `update()` 来绕过事件循环。
来源：[Qt 场景图说明][S1]、[Qt render loop 源码，1033–1036、1407–1452 行][S3]、
[QWindow 源码，2814–2851 行][S4]。

Qt 6.9 文档说明：平台支持时更新与显示垂直同步关联；否则回退延迟最多约 5ms，
屏幕报告刷新率超过 60Hz 时会缩短。基础实现的公式是从整数 5ms 起算，
高刷新率时除以 `refreshRate / 60` 再转为整数毫秒，使用 `Qt::PreciseTimer`。
例如报告 165Hz 时该基础路径的间隔是 1ms，不能将任意约 5ms 均值认定为该定时器。
`QT_QPA_UPDATE_IDLE_TIME=0` 只改变这条基础回退实现的间隔。
来源：[QWindow::requestUpdate 文档][S2]、[QPlatformWindow 源码，736–775 行][S6]。

### Windows D3D11 优先有另一条唤醒路径

Qt 6.9 Windows 的 `QWindowsWindow::requestUpdate()` 先检查
`QDxgiVSyncService::supportsWindow()`。有受支持输出时，登记一次回调并设置 pending，
由独立 DXGI vblank 线程回调，再向 GUI 线程投递 `deliverUpdateRequest()`；
否则才调用 `QPlatformWindow::requestUpdate()`。
因此在 DXGI 服务活跃的本机，仅设 `QT_QPA_UPDATE_IDLE_TIME=0` 可能没有作用。
来源：[Windows 窗口源码，3999–4020 行][S7]。

这一支持状态与实际图形适配器/输出关联：D3D11 RHI 设备创建成功后
`refAdapter(adapterLuid)`；swapchain 创建或重建成功后登记窗口。
服务从这些适配器寻找包含窗口中心的显示输出；没有匹配输出时 `supportsWindow()` 为假，
WARP 是官方源码明确列出的正常回退例子。窗口移到其他屏幕时通过队列连接重新匹配。
不能只因代码存在这一分支，就断定用户当前窗口一定走它。
来源：[D3D11 源码，397、5032、5468 行][S5]、
[DXGI 服务源码，285–418 行][S8]。

核实实际路径时，可在单独的短诊断运行启用 `qt.qpa.screen.updates.debug=true`，
检查 `Output for window ... actively used adapters ... refresh`、
`QDxgiVSyncThread ... entered run()` 或 `disabled by environment`。
该日志类别定义于源码，默认级别为 `QtCriticalMsg`；不应在正式性能采样中长期输出高频日志。
同时用 `qt.scenegraph.general` / `qt.rhi.*` 确认实际 render loop 与图形后端，
用 `qt.scenegraph.time.renderloop` 分析 polish、sync、render 与 swap。
来源：[DXGI 服务源码，16、47–70、91–99、339–365 行][S8]、[Qt 场景图日志说明][S1]。

### 调度唤醒与显示垂直同步是两件不同的事

Qt 6.9 源码的 `QT_D3D_NO_VBLANK_THREAD=1` 禁用上述 DXGI 唤醒服务，
会令窗口请求回到平台定时器路径。它不会直接将 `Present()` 的同步间隔改为 0。
因此“禁用 vblank 唤醒线程 + idle 0”的实验可以保持 Qt 默认呈交垂直同步，
用来检验新帧是否在窗口更新调度阶段额外等待。
这是指定 Qt 版本的私有环境变量，不是产品可依赖的稳定配置 API，也不是跨平台设置。
来源：[DXGI 服务构造，91–99 行][S8]、[Windows requestUpdate][S7]、[QRhiSwapChain 说明][S9]。

Qt 官方也明确说明，禁用呈交垂直同步不保证整个 GUI/render loop 无节流；
平台更新请求仍可限制调度。`QSG_NO_VSYNC` 或 swap interval 0 应独立测试，
不应与唤醒实验混在一组中，否则无法归因。副作用包括图像撕裂、帧节奏变化与功耗变化，
不能只看一个更小的阶段均值。
来源：[Qt 场景图垂直同步与更新调度说明][S1]、[QRhiSwapChain::NoVSync][S9]。

## D3D11 帧队列与等待位置

Qt 6.9 D3D11 的 `maxFrameLatency` 默认是 2，源码解释是在降低积压的同时保留
CPU/GPU 并行。采用 flip swapchain、启用呈交同步、使用真实硬件设备且配置非零时，
Qt 创建 frame-latency waitable object，并调用 `SetMaximumFrameLatency()`；
在每帧 `beginFrame()` 开头等待该对象。`WaitForSingleObjectEx()` 的 1000ms 参数是
等待超时上限，不是每帧固定延迟。
来源：[D3D11 私有头文件，769 行][S10]、
[D3D11 实现，229–241、1392–1394、5207–5215、5310–5325 行][S5]。

`QT_D3D_MAX_FRAME_LATENCY=1` 是另一个可单独做 A/B 的 Qt 6.9 私有开关。
其值是允许排队的 back-buffer 帧数上限，不能解释为固定额外 1 帧延迟。
0 在 Qt 这里意味着禁用 waitable object，绝不意味着“零帧排队”。
降低到 1 可能降低显示队列积压，也可能减少 CPU/GPU 并行，并让等待更早发生。
必须同时检查首包至 Qt 呈交、OUT 帧率/low、GPU/CPU 与真实显示延迟；
单独观察 `GuiWait` 甚至可能因为等待迁移而得出相反结论。
来源：[D3D11 实现][S5]、[Microsoft SetMaximumFrameLatency 定义][S11]。

`QRhiSwapChain::MinimalBufferCount` 控制的是可适用后端的 swapchain 图像数，
官方明确表示它不改变 frames in flight 的上限。它不是所有后端通用的“一帧低延迟”开关，
也不能与 DXGI maximum frame latency 混为一谈。
来源：[QRhiSwapChain::MinimalBufferCount][S9]。

## Ubuntu 的平台差异

Qt 6.9 XCB 的 `QXcbWindow` 及其 EGL、GLX、Vulkan 窗口没有覆盖 `requestUpdate()`，
使用上述 `QPlatformWindow` 基础定时器，因此零 idle 可以改变这段更新调度。
这并不取消 GL/Vulkan 呈交或桌面合成器的同步。
来源：[XCB 窗口声明][S14]及其[GLX][S15]、[EGL][S16]、[Vulkan][S17]派生窗口声明。

Wayland 的 `QWaylandWindow::requestUpdate()` 则有独立实现：
若正在等待 compositor frame callback，则保持等待；否则用 queued invocation
异步派发更新。frame callback 到达后再按 pending 状态派发更新。
该路径不读取 `QT_QPA_UPDATE_IDLE_TIME`，Windows 的 DXGI 开关也不适用于它。
Wayland compositor、输出刷新和窗口可见性必须作为单独的验证条件。
来源：[Wayland 更新请求、frame callback 请求与回调派发][S18]。

## 可执行的优化顺序

| 顺序 | 候选方案 | 有效条件与应验证的问题 |
| --- | --- | --- |
| 1 | 分开测 GUI 取帧、请求更新、beforeFrameBegin、同步 | 识别长尾发生于消息排队、平台唤醒、beginFrame 等待还是同步；保留同帧关联 |
| 2 | 在独立进程中对照默认 / idle 0 / 禁 DXGI 唤醒 / 两者组合 | 验证当前 Windows 的平台唤醒路径；保持呈交同步、源、窗口、负载一致 |
| 3 | 独立对照 D3D11 maximum frame latency 1 与默认 2 | 检查显示队列和吞吐权衡；不要混入禁同步等其他变量 |
| 4 | 将最新帧快照读取推迟至场景图同步，必要时 preprocess | 降低等待期间旧快照被锁定的机会；需要渲染线程可安全访问的最新帧邮箱 |
| 5 | 继续规划硬件纹理互操作 / 平面 YUV 渲染 | 主要减少下载、转色、上传与内存带宽，不能保证直接消除平台调度等待 |

当前 GUI 已采用单次合并唤醒与最新帧邮箱，不存在必须再删除的固定 16ms 视频轮询。
250ms 定时器只刷新诊断。仍可检查 `poll()` 发射视频通知前有无昂贵的属性更新、
文本布局与其他槽，及逐帧 `contentRectChanged()` 是否触发不必要的 QML 重算；
只有实际分段确认这些工作占比后才应修改。
现有 `basic` render loop 本机对照更慢，不能仅因单线程路径更短就改用它，
见[先前运动 GOP 对照](../../validation/video/latency_investigation.md)。

### 较晚读取最新帧的边界

最小的架构候选是让 GUI 仅通知有新画面并请求更新，渲染同步阶段再从受控邮箱读取
最新只读帧，而不是把 GUI 回调时刻的帧一直固定到稍后的同步。
它可以让在等待期间到达的新帧更早被这一轮绘制采用，不能保证提前这一轮绘制本身。
旧帧被替换时要记录丢弃/覆盖，不得通过只统计较新的帧而掩盖吞吐损失。

更晚的节点处理可以使用 `QSGNode::preprocess()`：设置 `UsePreprocess` 后，
每轮该节点实际被渲染前调用，可作最后的纹理准备。该阶段 GUI 可能与渲染线程并行，
只能访问明确线程安全的渲染快照/邮箱，不可直接读取或修改任意 QML/GUI 对象。
纹理仍由场景图渲染线程管理，需要处理来源代数、停流清屏、窗口隐藏、设备丢失、
场景图重建与销毁顺序。阻塞子树不调用 preprocess，且它自己不调度新一轮绘制。
来源：[QSGNode::preprocess 与线程说明][S12]、[场景图同步时序][S1]。

`beforeSynchronizing()` 保证 GUI 被阻塞，可用于同步准备；`beforeFrameBegin()` 是
渲染线程最早的逐帧信号。`scheduleRenderJob()` 除 NoStage 外只是等待既定渲染阶段，
本身不触发绘制，不是能跳过窗口调度的快捷入口。
来源：[QQuickWindow 信号与 scheduleRenderJob][S13]。

将视频拉取推到渲染线程不等于允许解码线程操作场景图。若采用 render-thread 自驱绘制，
需要重新设计唤醒、停止与生命周期；Qt 私有实现中有专用于渲染线程动画的重绘路径，
不能将其当作任意后台线程可调用的公开契约。
`QQuickRenderControl` 可完全接管 polish/sync/render，但也将完整调度责任交给应用，
目前应先验证上述局部路径，再评估是否值得大规模改造。
来源：[Qt threaded loop 的 update / maybeUpdate 线程检查][S3]、[QQuickRenderControl 说明][S1]。

## 本轮本机对照结果

Windows 11、Qt 6.9.0、Debug 构建、NVIDIA D3D11VA / Direct3D 11、屏幕报告 165Hz。
使用现有 `hdVideoReportsActualBackendAndPresentation`，发送与[先前测量][L1]相同的
240 帧、1920×1080 / 60fps、无 B 帧 HEVC 运动 GOP。每组独立进程顺序运行 10 秒，
仅取末尾最近 1 秒快照，约 57–61 个耗时样本；表中区间为各次快照数值范围，
不是置信区间或连续 10 秒的总体分位数。原客户端、MockServer 和 FFmpeg 仍在后台运行。
窗口为测试默认 1280×720、Esc 菜单关闭，与用户截图打开设置时的负载不同。

按 A 默认 → B idle 0 → C 禁 DXGI 唤醒 → D 二者组合 → E 默认 → F 组合 →
G 默认 → H 组合 → I maximum frame latency 1 的顺序执行。
全部保留 threaded render loop、默认呈交垂直同步和默认帧队列，I 组仅将帧队列上限改为 1。
实验环境变量仅存在于测试子进程，未保存到项目启动、用户环境或原客户端。

| 配置 | 次数 | 输出至同步均值 ms | 输出至同步 P95 ms | 首包读出至 Qt 呈交均值 ms | 首包读出至 Qt 呈交 P95 ms | OUT 快照 fps |
| --- | --- | --- | --- | --- | --- | --- |
| 默认 | 3 | 4.744–5.195 | 7.820–8.379 | 13.220–14.767 | 17.545–18.369 | 57–59 |
| 仅 `QT_QPA_UPDATE_IDLE_TIME=0` | 1 | 4.774 | 7.708 | 12.906 | 18.915 | 59 |
| 仅 `QT_D3D_NO_VBLANK_THREAD=1` | 1 | 2.384 | 3.603 | 10.219 | 12.267 | 60 |
| 禁 DXGI 唤醒 + idle 0 | 3 | 1.610–3.431 | 1.911–6.208 | 9.574–11.832 | 11.811–16.726 | 60 |
| 仅 `QT_D3D_MAX_FRAME_LATENCY=1` | 1 | 4.866 | 9.294 | 12.713 | 17.311 | 58 |

默认与组合交替复测，均观察到组合的输出至同步均值较低，说明平台更新唤醒是值得优先处理的
优化方向；末次组合仍有明显波动，不能只挑 1.610ms 当成稳定上限。
单设 idle 0 未显示明确改善，与默认 DXGI 路径绕过基础定时器的源码一致。
仅设 maximum frame latency 1 的一轮没有显示明确的同步等待改善，尚不能作为推荐默认值。
各组解码均值也在约 5.19–6.46ms 波动，不能将整段总耗时变化全部归因于调度。

另开一次 2.5 秒诊断进程，启用 Qt 日志，确认默认窗口为 threaded / D3D11，
maximum frame latency 为 2，窗口匹配到实际显示输出，`QDxgiVSyncThread` 已进入运行。
该高日志运行只核实路径，不混入性能表。
所有 9 次性能调用与 1 次路径诊断均通过现有测试；没有修改业务代码，也没有重跑完整 CTest。
原始日志保留在忽略的 `.cache/archive/flat/render_wait_*.txt`，不提交机器专用产物。

所有性能组本地待解码累计丢弃均为 0，但不能据此证明所有帧都物理显示。
组合组 OUT 1% low 约 22.40–27.11fps，发送源 1% low 约 27.32–31.62fps，
说明降低这一段等待没有解决全部长尾。idle 0 单项运行甚至出现 OUT 1% low 约 7.28fps，
需要较长时间线定位异常，不能仅凭均值选择方案。
本轮没有测 CPU/GPU 占用、功耗、窗口交互、物理亮屏、30 分钟长稳或 Ubuntu/其他显卡；
这些是默认采用调度策略前的必要验证，短测不能视为兼容性验收。

## 关闭呈交垂直同步的补充对照

用户进一步询问关闭垂直同步的效果后，沿用以上机器、Debug 构建、源、窗口和采样方式，
顺序运行 5 组各 10 秒测量。`QSG_NO_VSYNC=1` 请求 Qt 关闭呈交同步；
“优化唤醒”仍指禁 DXGI 唤醒服务与 idle 0 的组合。它们控制不同环节。
独立 2.5 秒日志核实 Qt 已打印 swap interval 0、禁用 vblank 服务和 threaded render loop。

| 顺序 / 配置 | 输出至同步平均 / P95 ms | 首包至 Qt 呈交平均 / P95 ms | OUT 快照 fps |
| --- | --- | --- | --- |
| A 默认唤醒、同步开启 | 5.645 / 8.613 | 13.598 / 17.503 | 60 |
| B 默认唤醒、同步关闭 | 4.503 / 7.922 | 12.827 / 16.102 | 55 |
| C 优化唤醒、同步关闭 | 2.472 / 4.261 | 10.928 / 14.292 | 60 |
| D 优化唤醒、同步开启 | 3.365 / 5.816 | 12.090 / 16.313 | 60 |
| E 优化唤醒、同步关闭 | 2.282 / 3.430 | 11.112 / 17.154 | 59 |

5 组性能调用和 1 次日志调用均通过现有测试，本地待解码累计丢弃均为 0。
关闭同步两次与中间恢复同步相比，呈交均值较低，但 E 的总耗时 P95 反而高于 D，
因此不能宣称它稳定改善长尾。后台负载与发送节拍仍波动，未改变原客户端配置。
原始日志位于忽略的 `.cache/archive/flat/vsync_*.txt`。

垂直同步主要避免扫描过程中切换到另一帧造成撕裂，并使呈交节奏受刷新率约束；
接收 UDP、组帧和解码不要求同步开启。关闭同步可减少等待，但不能增加源视频的有效帧数。
Qt 官方说明 `NoVSync` 为后端相关的请求，可能被忽略；Windows 桌面合成器、
Linux X11 合成器与 Wayland compositor 仍可约束实际显示，不能保证中途立即扫描新画面。
所以首包至 `frameSwapped` 变短只证明应用提交更早，不能当作已测物理显示收益。
来源：[Qt 同步与更新调度说明][S1]、[NoVSync 公开契约][S9]、[frameSwapped 定义][S13]。

此结果支持将同步开启/关闭作为进一步双平台验证的候选策略，不足以直接全局关闭。
必须保持同一窗口中视频与独立业务叠层的正确合成，并在两种系统上测撕裂、延迟、
帧间隔、功耗及持续运行；Wayland 是否允许实际异步呈现取决于 compositor 与可用协议。

## 验收与边界

收益应由同源、同后端、同刷新率、前后交替重复实验中的同帧首包至 Qt 呈交分布确认，
并同时要求 OUT 帧率、5%/1% low、覆盖丢帧、UI 交互、CPU/GPU 占用无不可接受退化。
最近 1 秒约 60 样本的 P95/P99 易波动，应保留持续采样与较长窗口的原始样本，
不能相加各阶段 P95，也不能相减独立运行的 P95 来量化某阶段贡献。

`frameSwapped()` 的公开定义是帧已排入呈交，不能测出屏幕像素真正亮起。
桌面合成器、驱动队列、显示刷新与面板扫描仍可能增加延迟，且不是固定再加一个刷新周期。
改变调度后应结合外部高速摄像/光电或经验证的显示时间戳复核，避免把等待从测量终点前
移到终点后误称为用户看见画面更快。
来源：[QQuickWindow::frameSwapped][S13]。

Windows D3D11 实验必须与 Ubuntu X11 / Wayland 分开验收。
项目所有功能以 Windows 与 Linux 同时可用为完成条件，Ubuntu 为当前 Linux 验收基线；
本轮 Windows 开关实验只用于定位，正式方案必须具备 Linux 实现，不能只交付 Windows 优化。
Intel Core i / Core Ultra 核显、NVIDIA、混合显卡与无可用硬件加速均需保持可用路径；
DXGI 专有调度开关不应透传成通用 UI 配置。Qt 版本升级时重新核对私有开关、
默认帧队列与日志，不能仅凭变量名继续沿用。

## Qt 版本状态补充（2026-11-13）

联网核实的最新稳定版本为 **Qt 6.12.0 LTS**，官方发布公告和 release wiki 均记载
2026-09-30 发布；官方稳定下载目录的 6.12 分支当前只有 6.12.0，
官方支持版本表也将 6.12 的最新版本列为 6.12.0。此结论来自已发布信息，不依据路线图推算。
来源：[6.12 LTS 发布公告][V1]、[稳定下载目录][V2]、[版本及支持周期][V3]。

项目当前使用的 Qt 6.9.0 发布于 2025-04-02，6.9 不是 LTS；
官方表列出的 6.9 最新补丁是 6.9.3，标准支持状态为已结束，可另行购买扩展支持。
Qt 6.8 起每第四个次版本提供 LTS，初始公开补丁与后续商业 LTS 补丁的可获取性不同。
因此不应继续将 6.9.0 描述为当前最新版本，也不能将 LTS 的完整商业支持期限
等同于所有开源补丁立即公开可用。
来源：[6.9 发布公告][V4]、[官方版本与支持说明][V3]。

Qt 升级与降低延迟应分别验证。6.12 是较新的受支持基线候选，但升级本身不能保证
消除输出至渲染等待；需重新核对 Windows/Linux 构建、Qt MQTT、编译器和部署依赖，
并重新测量本报告中的调度路径及私有环境变量，不能直接沿用 6.9 的实现结论。

## Qt 6.12 与当前等待路径的直接变化

将 Qt 官方 `v6.9.0` 与 `v6.12.0` 源码逐项对照，可确认以下相关改进。
这证明新版包含针对调度和生命周期的修复，不构成升级后本项目延迟数值的实测。

| 环节 | Qt 6.12 的变化 | 对本项目的意义与限制 |
| --- | --- | --- |
| Windows 无同步窗口 | DXGI 服务的 `registerWindow()` 新增 `window->format().swapInterval() == 0` 时跳过注册 | 关闭同步的窗口不会再通过此服务等待 vblank 后才更新；与 6.9 单关同步仍保留唤醒等待的问题直接相关，仍有平台定时器、GUI 和呈交等待 |
| Windows 更新事件 | `requestUpdate()` 用 Ready / Requested / Posted 三态只保留一次已投递事件，DXGI 接手时停止旧 fallback timer | 避免 GUI 忙时重复投递或两条机制同时交付；不能据此承诺均值减少固定毫秒 |
| Windows 窗口销毁 / 重建 | 回调校验当前平台窗口与 callback ID，注册窗口销毁时自动清理服务条目 | 减少旧回调触及新/已销毁窗口的风险，尤其涉及重建窗口的方案 |
| D3D11 开始帧 | `lastFrameLatencyWaitSlot` 保证只有上次实际 `Present()` 后才再次等待对象 | 避免跳过呈交的路径重复等待直至约 1 秒超时；不是正常连续呈交每帧固定省 1 秒 |
| 通用更新定时器 | 先交付更新，确认没有 pending request 后才停止定时器；支持交付过程中重启 | 改善暂缓交付与重入更新的稳定性，idle 默认公式并未取消 |
| Qt Quick threaded loop | 隐藏造成零尺寸返回前唤醒等待 GUI；重新 exposure 用事件安全交接窗口；polish 后重新核对窗口有效性 | 关闭同步验收应包含最小化、恢复、缩放和退出，升级后的稳定性修复与这些场景相关 |

来源：[6.12 DXGI 服务，382–419 行][N1]、[6.12 Windows requestUpdate，4098–4147 行][N2]、
[6.12 D3D11 beginFrame，1489–1496 行][N3]、
[6.12 通用定时器交付与更新实现][N4]、[6.12 threaded render loop][N5]。
额外检查 `v6.9.3`、`v6.10.0`、`v6.11.0` 的 DXGI 服务文件，未见上述 swap interval 0
跳过注册的分支；本记录不据此推断该修复最先进入的补丁版本或所有回移状态。

同步开启时，6.12 仍保留 DXGI vblank 唤醒及 D3D11 waitable object，
默认 maximum frame latency 也仍是 2。因此“直接升级就消除当前约 5ms 等待”的判断不成立。
关闭同步时的新 DXGI 分支更直接相关，后续[独立 A/B](../../validation/qt612.md)
使用相同码流、显示器和产品设置核实其实际收益。
来源：[6.12 默认帧队列][N6]、[6.12 服务与窗口更新][N1][N2]。

Linux 方面，X11/XCB 仍继承通用更新定时器，基础 idle 计算与 6.9 相同。
Wayland 6.12 已从 qtwayland 移到 qtbase；`requestUpdate()` 仍在等待 compositor
frame callback 时推迟交付，不能把 Windows 的改善推广为 Linux 的相同收益。
Wayland EGL 的 swap interval 0 请求可以避免 Qt 模拟同步阻塞，但最终异步呈现仍受
compositor、驱动和协议能力约束。来源：[6.12 Wayland 更新及 callback][N7]、
[6.12 Wayland EGL swap][N8]、[6.12 基础更新实现][N4]。

### 垂直同步设置的生效契约

Qt 6.9 与 6.12 的公开 `QWindow::setFormat()` 契约都说明：
窗口 `create()` 后设置格式不会重新解析现有 native surface。
`QSurfaceFormat::setSwapInterval(0/1)` 是请求值，底层不支持时可能忽略。
为同时覆盖 Windows / Linux 及不同图形后端，稳定的基础实现应在首个窗口创建前设置
默认格式，界面保存偏好后显示重启生效；运行中只改一个属性不能宣称呈交策略已切换。
如将来要求即时切换，须另行完整设计窗口、场景图、纹理和交互状态的销毁与重建。
来源：[6.9 setFormat 契约][N9]、[6.12 相同契约][N10]、[swap interval 公开定义][N11]。

另有容易误判的实现细节：Qt 6.9 `QSGDefaultContext::defaultSurfaceFormat()` 用
`static bool disableVSync = qEnvironmentVariableIsSet("QSG_NO_VSYNC")` 读取变量。
此实现判断变量是否存在，`QSG_NO_VSYNC=0` 仍会请求关闭，而且首次读取后缓存。
产品应使用明确的启动格式设置并诊断外部覆盖，测试切换使用独立进程；
不能在运行中反复修改该环境变量伪装成即时生效。
来源：[Qt 6.9 defaultSurfaceFormat，176–200 行][N12]。

## 一手来源

[S1]: https://doc.qt.io/qt-6.9/qtquick-visualcanvas-scenegraph.html
[S2]: https://doc.qt.io/qt-6.9/qwindow.html#requestUpdate
[S3]: https://github.com/qt/qtdeclarative/blob/v6.9.0/src/quick/scenegraph/qsgthreadedrenderloop.cpp
[S4]: https://github.com/qt/qtbase/blob/v6.9.0/src/gui/kernel/qwindow.cpp#L2814
[S5]: https://github.com/qt/qtbase/blob/v6.9.0/src/gui/rhi/qrhid3d11.cpp
[S6]: https://github.com/qt/qtbase/blob/v6.9.0/src/gui/kernel/qplatformwindow.cpp#L736
[S7]: https://github.com/qt/qtbase/blob/v6.9.0/src/plugins/platforms/windows/qwindowswindow.cpp#L3999
[S8]: https://github.com/qt/qtbase/blob/v6.9.0/src/gui/rhi/qdxgivsyncservice.cpp
[S9]: https://doc.qt.io/qt-6.9/qrhiswapchain.html
[S10]: https://github.com/qt/qtbase/blob/v6.9.0/src/gui/rhi/qrhid3d11_p.h#L769
[S11]: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency
[S12]: https://doc.qt.io/qt-6.9/qsgnode.html#preprocess
[S13]: https://doc.qt.io/qt-6.9/qquickwindow.html
[S14]: https://github.com/qt/qtbase/blob/v6.9.0/src/plugins/platforms/xcb/qxcbwindow.h
[S15]: https://github.com/qt/qtbase/blob/v6.9.0/src/plugins/platforms/xcb/gl_integrations/xcb_glx/qxcbglxwindow.h
[S16]: https://github.com/qt/qtbase/blob/v6.9.0/src/plugins/platforms/xcb/gl_integrations/xcb_egl/qxcbeglwindow.h
[S17]: https://github.com/qt/qtbase/blob/v6.9.0/src/plugins/platforms/xcb/qxcbvulkanwindow.h
[S18]: https://github.com/qt/qtwayland/blob/v6.9.0/src/client/qwaylandwindow.cpp
[L1]: ../../validation/video/latency_investigation.md
[V1]: https://www.qt.io/blog/qt-6.12-released
[V2]: https://download.qt.io/official_releases/qt/6.12/
[V3]: https://doc.qt.io/qt-6/qt-releases.html
[V4]: https://www.qt.io/blog/qt-6.9-released
[N1]: https://github.com/qt/qtbase/blob/v6.12.0/src/gui/rhi/qdxgivsyncservice.cpp#L382
[N2]: https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/windows/qwindowswindow.cpp#L4098
[N3]: https://github.com/qt/qtbase/blob/v6.12.0/src/gui/rhi/qrhid3d11.cpp#L1489
[N4]: https://github.com/qt/qtbase/blob/v6.12.0/src/gui/kernel/qplatformwindow.cpp
[N5]: https://github.com/qt/qtdeclarative/blob/v6.12.0/src/quick/scenegraph/qsgthreadedrenderloop.cpp
[N6]: https://github.com/qt/qtbase/blob/v6.12.0/src/gui/rhi/qrhid3d11_p.h#L879
[N7]: https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/qwaylandwindow.cpp#L1762
[N8]: https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/plugins/hardwareintegration/wayland-egl/qwaylandglcontext.cpp#L501
[N9]: https://github.com/qt/qtbase/blob/v6.9.0/src/gui/kernel/qwindow.cpp#L907
[N10]: https://github.com/qt/qtbase/blob/v6.12.0/src/gui/kernel/qwindow.cpp#L931
[N11]: https://doc.qt.io/qt-6.9/qsurfaceformat.html#setSwapInterval
[N12]: https://github.com/qt/qtdeclarative/blob/v6.9.0/src/quick/scenegraph/qsgdefaultcontext.cpp#L176