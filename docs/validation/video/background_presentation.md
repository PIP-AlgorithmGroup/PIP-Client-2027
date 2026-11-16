# 前后台视频呈交诊断与架构对照

日期：2026-11-16。基线 `18647dc`，Qt 6.12.0、FFmpeg 7.1。本次检查的对象已经是
GPU YUV 通路，不能沿用改造前“每帧必须下载、CPU 转色”的归因。

## 测量方法与范围

Windows 11、i7-14700HX、RTX 4070 Laptop、595.79 驱动。实验期间存在外接 MG24Q/165Hz
与内屏/150% DPI 的显示环境变化；最后几何验证记录为 MG24Q、165Hz、DPR 1。
同机仍有用户的客户端与 ffmpeg 运行，没有为了跑分停止它们；构建、回归和性能实验分开，
性能对照串行运行。因此结果是本机受控对照，不是独占机器或全硬件矩阵的帧率保证。

Qt 实验使用完整产品 QML、真实回环 UDP、1080p60 HEVC、240 AU 无 B 帧 GOP，
threaded 渲染循环、关闭垂直同步，要求实际 `d3d11-shared-yuv`，并断言下载/CPU 转色无样本。
分别观察前台、失焦但可见、独立进程完全遮挡、最小化、恢复和 GUI 注入停顿六阶段。
遮挡窗口由独立进程创建，避免同进程仍有可见窗口改变 Windows 后台策略。

时间统一为本机 `steady_clock` 微秒。记录输入发送、首包读出、解码引用就绪、GUI 取帧、
刷新请求附近、beforeFrameBegin、同步、绘制及 frameSwapped；只统计带新视频帧号的呈交。
GUI 事件另外记录超过 2ms 的最外层 notify；区间可能包含 OS 等待，不是纯 CPU 时间。
数据先存有界内存，结束才写盘。每阶段前后各剔除 1.5 秒，避免启动/终止遮挡进程的边界开销。
部分运行中 Qt 缓存的 `isActive()` 与 OS 前台 HWND 不一致，因此最终重测以 Windows
`GetForegroundWindow()` 判定激活状态，并用窗口中心点命中遮挡进程验证完全遮挡。
只标记 Qt active 的中间实验不用于最终前后台对比；工具会拒绝未取得预期失焦状态的运行。
最终数据中，名为 `foreground` 的初始阶段实际 OS active 为 0；下文统一称为“初始可见”，
不能作为已激活前台。`recovered` 阶段实际取得前台焦点，各组稳定区间 active 为全部样本。
最新几何验证使用 GetClientRect/ClientToScreen 原生坐标，避免混合 DPI 下 Qt 逻辑坐标
与 WindowFromPoint 像素坐标不一致；旧格式未记录屏幕/DPI 的数据不用于证明中心命中。

报告 low1 为该稳定阶段最慢 1% 呈交间隔均值的倒数，至少 100 个间隔。
它与产品界面的最近 10 秒滚动 low 不是相同窗口。frameSwapped/Present 返回均不是显示器亮屏，
本次没有 PresentMon/GPUView、GPU 时间戳、光电或功耗测量。

## 已证实的问题

### 测试输入节拍会污染 low

原发送器使用 `std::this_thread::sleep_until()`。基线前台输入平均间隔 16.666ms，
P99 为 32.070ms、最大 32.875ms、low1 为 30.77fps；输出 low1 为 29.74fps。
不能把这一部分全部算到 Qt。Windows 高精度 waitable timer 改变的只是诊断发送器，
不代表相机或用户正在使用的外部发送程序也已修复；真实发送端仍需记录 AU/到包节拍。

本地 GOP 为 2,012,510 字节，SHA-256：
`8a3af4c879263d0cb1fdde2600aafc80fa9ece03f0ec3a7cd89166b455116ef1`。

高精度源前台输入 P99 17.305ms、最大 17.375ms、low1 57.64fps 后，
旧 Qt 通路仍出现输出最大间隔 36.638ms、low1 28.39fps，说明确实还有客户端额外抖动。

### 周期网卡查询占用 GUI

`MatchSession` 原来每 500ms 在 GUI 定时器内调用 `checkNetwork()`，进一步调用
`QNetworkInterface::interfaceFromName()`。即使未配置会话网卡，原逻辑也先查系统接口，
再判断配置是否为空。网卡/驱动枚举会等待 OS，不能把它看成廉价的内存字段读取。

原生事件记录显示每约 500ms 出现 12–29ms 的 QTimer 事件；GUI 取帧前等待随之增加。
移除诊断叠层后仍有该周期停顿；改后台计时策略、`QT_QPA_UPDATE_IDLE_TIME=0`
也未稳定消除长尾。同步、绘制和 Present 的 CPU 观测成本远小于这些停顿。
这是本机可复现的具体阻塞来源；原用户日志没有 OS 窗口状态、逐事件调用栈或 GPU/调度跟踪，
不能据此把原日志每一次 54ms 的 `renderWait` 都归到网卡枚举，或排除调度器/驱动等待。

修正将周期网卡查询放入独立任务，最多一个在途，忙时合并轮询；只传配置值副本，
结果回 GUI 后检查配置代数，丢弃旧查询结果。未选网卡时直接判定未就绪。
显式保存、启动和手动刷新网卡仍同步校验；用户操作可能短暂阻塞，不能声称所有 GUI
系统查询均已异步。析构 join 当前 OS 查询，OS 调用本身不能强制取消。

### GUI 停顿与延迟统计盲区

受控阻塞 GUI 250ms 时，Qt 输出最大间隔达到约 256–292ms；一组实验中
已呈交帧的首包至呈交最大仅 16.965ms。期间旧帧被合并/覆盖，恢复后处理最新帧，
所以“已显示帧延迟较小”无法证明“中间没有冻结”。

即使没有丢帧，若输入间隔为 16.67ms，连续两帧延迟分别 1ms 和 18ms，输出间隔也会达到
33.67ms。单帧延迟不能直接取倒数当 low。原日志最近 1 秒延迟和最近 10 秒 low 的时间窗
不同，也可能在停顿过去后继续显示低 low。

## 独立呈交对照

`video_native_present_probe` 是手动 Windows 架构实验，不是已经集成的产品后端。
仍使用同一 `HevcDecoder` 和同一 GOP，但直接定时喂 AU，省略 UDP/组帧；
因此其 source→Present 不能冒充首包→Qt 呈交，也不能直接相减作为迁移节省的毫秒数。

解码、Win32 建窗/消息循环/呈交与 Qt GUI 分属线程，单槽最新帧邮箱、事件唤醒。
使用解码设备的 D3D11 context，在 FFmpeg 设备锁内做 NV12 GPU 复制和 shader 转色，
flip-discard 双缓冲交换链 Present(0)。没有常态 CPU 下载或像素上传。
仅预热首帧使用静态四象限码流，下载输出与 CPU 参考比较，最大每通道误差 1/255。
颜色校验不进入测量阶段；支持范围仅 SDR NV12、BT.601/709，不涵盖 P010/HDR 或产品 HUD。

最终独立呈交对照的 GUI 阻塞阶段最大间隔 18.985ms，没有传播 250ms 停顿。
这证明独立线程/呈交能隔离 GUI 卡顿，但并不保证所有环境 low1≥50，也不是完整 UI 迁移验收。

SDL2/SDL3 的普通 RenderPresent 仍有 main-thread 契约，SDL3 GPU API 的窗口/交换链操作
也有创建线程约束；本轮没有执行 SDL 后端 A/B，不能声称 SDL3 必然快多少。
若仍与繁忙 GUI 共用线程，仅替换库不能获得本实验的隔离效果。
若要隔离视频，原生交换链或独立 SDL 进程/主循环均须明确设计 GPU 共享、HUD、输入和恢复。
官方来源与 Ubuntu/Intel 互操作限制见[呈交选型研究](../../research/video/sdl_presentation_options.md)。

## 最终实测结果

下表为每阶段稳定区间的 low1 fps；最小化时按策略停止呈交，不列 low。
数据名对应本地 `.cache/gpu_pipeline/background_probe/` 中的原始 JSON 和 summary。
Qt 的 `os_*` 组均使用高精度发送器并显式启用诊断计时守卫，不能当作产品默认后台策略。
独立呈交器使用自身高精度发送源，不使用 Qt 计时守卫。各阶段、运行间输入和显示环境
仍有变化，表格用于判断趋势与机制，不用于宣称精确百分比收益。

| 数据名/方案 | 初始可见（未激活） | 失焦可见 | 完全遮挡 | 恢复前台 | GUI 阻塞阶段最大间隔/ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| os_fixed_a：Debug，已修正 | 34.4 | 43.2 | 44.7 | 50.0 | 256.665 |
| os_replay_a：重放原同步查询 | 23.1 | 27.2 | 23.6 | 37.2 | 278.556 |
| os_configured：真实异步网卡检测 | 43.2 | 44.8 | 39.4 | 29.9 | 272.081 |
| os_replay_b：再次重放 | 18.3 | 22.7 | 22.2 | 34.2 | 256.122 |
| os_fixed_b：再次修正对照 | 40.1 | 37.3 | 27.7 | 46.2 | 259.500 |
| os_idle：另设 idle=0 | 44.1 | 43.2 | 39.9 | 45.7 | 264.188 |
| os_release_retry：Release，已修正 | 48.7 | 48.7 | 50.5 | 43.2 | 260.994 |
| os_native：独立 D3D11 实验 | 46.6 | 52.5 | 48.6 | 53.2 | 18.985 |

两次重放同步查询均加重长尾，移到后台有直接因果依据；修正后仍未稳定达到 low1≥50。
独立呈交并非每阶段更快：初始可见阶段这次甚至略低于 Qt；明确优势是 GUI 停顿隔离。

Release 的恢复前台阶段，首包读出→Qt 呈交平均 **1.815ms**，P95 **2.773ms**、
P99 **4.760ms**、最大 **16.643ms**。同一阶段分段如下，均为 CPU 时钟观测，
不是 GPU 执行时间；各分段最大值可能不是同一帧，不能相加。

| 区间 | 平均/ms | P99/ms |
| --- | ---: | ---: |
| 首包→组帧/解码引用就绪 | 0.268 | 0.519 |
| 解码就绪→GUI 取帧 | 0.186 | 2.007 |
| GUI 刷新请求附近→beforeFrameBegin | 0.978 | 2.191 |
| beforeFrameBegin→同步开始 | 0.006 | 0.017 |
| 场景图同步 | 0.228 | 0.471 |
| 绘制回调区间 | 0.035 | 0.062 |
| afterRendering→frameSwapped | 0.069 | 0.114 |

初始可见阶段首包→呈交平均 1.874ms、P99 5.032ms、最大 7.160ms，输入 low1 57.89fps，
输出 low1 48.69fps。更能说明统计盲区的是 GUI 阻塞阶段：已呈交帧延迟最大仅 11.848ms，
输出最大间隔却有 260.994ms，呈交帧号间跳过 14 个 AU。独立呈交在对应阶段跳过编号为 0，
low1 为 54.93fps；source→Present 约 0.64ms，但其省略 UDP，不直接与 Qt 端到端延迟相减。

最后 `geometry_final` 短测未启用计时守卫，功能通过，OS 失焦和原生中心命中校验通过，
所有阶段记录了屏幕/DPI。low1 为初始可见 30.15、失焦 29.12、遮挡 27.19、恢复前台 50.32fps，
输入对应为 46.13、39.98、50.92、58.17fps。初始可见的 GUI→begin 平均 7.431ms，
恢复后为 0.962ms，说明默认后台策略仍值得单独优化；这组同时存在输入抖动，且稳定区间较短，
不能把与其他组的差异全部归于计时守卫。诊断守卫已确认策略 control=4/state=0 生效，
但多组实验表明它和 idle=0 都不足以保证 low1≥50；本轮未将其加入独立视频的产品生命周期。

首次 Release 手动专项在阶段测量结束后的 decodeFps 断言失败，末尾出现 PPS 错误；
该次没有作为性能成功样本。后续重跑功能通过，表中使用 `os_release_retry`；首次异常
未完整定位，不能把重跑成功写成从未失败。

剩余调度/驱动长尾未完全定位。`wpr -start CPU -filemode` 被 Windows 策略拒绝，
错误 `0xc5585011`（Failed to enable the policy to profile system performance）；
`wpr -status` 确认没有遗留录制。本次未取得 ETW CPU/GPU 调度证据，不能进一步断言
每次后台卡顿究竟由 Qt 更新定时、OS 调度、DWM 还是驱动引起。

建议先保留现有 Qt/GPU 通路并修复 GUI 阻塞；若产品要求 GUI 停顿时视频仍连续，
独立视频线程与交换链已经证明有改善可能，下一阶段应补齐 HUD/输入和 Ubuntu 后端。
SDL3 可作为独立视频主循环的实现选项，但普通同线程换库没有实测收益保证。

## 复现

先使用项目固定依赖完成构建；将所选 Qt kit 的 bin/platforms 加入本地运行环境。
工具不下载 SDK、SDL 或码流，不依赖某个开发机盘符。输入为含 AUD 的 HEVC Annex B GOP。
例子中的本地 GOP 沿用本次缓存；其他机器需自行生成或显式指定等价 1080p60、无 B 帧测试源。

```powershell
cmake --build build/windows/qt612-debug --target welcome_ui_test video_window_cover video_native_present_probe
python tools/run_video_presentation_probe.py --build-dir build/windows/qt612-debug --stream .cache/archive/flat/latency_gop.hevc --output-dir .cache/gpu_pipeline/background_probe --name fixed --precise-pacer
python tools/run_video_presentation_probe.py --build-dir build/windows/qt612-debug --stream .cache/archive/flat/latency_gop.hevc --output-dir .cache/gpu_pipeline/background_probe --name replay --precise-pacer --replay-network-poll
python tools/run_video_presentation_probe.py --build-dir build/windows/qt612-debug --stream .cache/archive/flat/latency_gop.hevc --output-dir .cache/gpu_pipeline/background_probe --name configured --precise-pacer --configured-network
python tools/run_video_presentation_probe.py --build-dir build/windows/qt612-release --stream .cache/archive/flat/latency_gop.hevc --output-dir .cache/gpu_pipeline/background_probe --name native --native
python tools/analyze_video_presentation.py .cache/gpu_pipeline/background_probe/fixed.json
```

`--replay-network-poll` 仅在诊断工具中重放原先 GUI 每 500ms 的系统查询，用于相同二进制的
因果 A/B；`--configured-network` 保存临时回环会话配置，验证真实异步网卡查询。
`--timer-guard`、`--idle-ms 0`、`--no-overlay` 分别隔离后台计时、更新调度和诊断叠层。
普通 CTest 不启用这些实验变量；性能阈值失败返回 1，与功能测试失败分开报告。
最小化阶段没有呈交属于隐藏窗口策略，不拿零 FPS 当可见画面的性能失败。

## 验证边界

新增回归覆盖慢查询不占用 GUI、最多一个在途查询、配置变化后忽略旧结果，以及活动会话
断网/恢复仍保留启动意图。查询故障由有界同步原语注入，不依赖拔用户网线或机器特定查询速度。

本轮最终验证：

| 验证 | 结果 |
| --- | --- |
| Windows Debug 完整构建/CTest | 19/19，180.47s |
| Windows Release 完整构建/CTest | 19/19，180.34s |
| Ubuntu 22.04/WSL 构建、all_qmllint、CTest | 18/18，151.59s |
| Windows 原生 D3D11 纹理专项 | 28/28，无跳过 |
| Windows 原生 OpenGL 纹理专项 | 28/28，无跳过 |
| 最新 DPI/屏幕采样后的 Debug/Release/Ubuntu 增量回归 | 各 2/2：match_session、welcome_ui |
| Windows Release 安装暂存 | cmake --install 成功 |
| 最新原生几何/前后台短测 | 功能通过，low1≥50 性能门槛失败 |

网卡会话测试含初始化/清理共 11 项通过。C++ clang-format、git diff --check 和 Python
语法检查通过。手动矩阵中的 LOW_CHECK FAIL 表示性能阈值未达标，不包装为全项验收通过。

Intel 核显、AMD、混合显卡、原生 Ubuntu VAAPI/X11/Wayland、物理网络切换和长期功耗仍须实机验收。
WSL 的软件 FFmpeg/离屏回归只能验证 Linux 通用逻辑；不能作为 Ubuntu GPU 互操作通过证据。
本次没有把 Windows 原生试验工具作为跨平台产品功能发布。
