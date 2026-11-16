# SDL 与独立视频呈交选型补充

日期：2026-11-16。本文是官方文档和源码调查，未构建或运行 SDL 对照程序，
不提供收益毫秒数。SDL 文档为当日官方当前页，源码细节固定到 release-3.2.0；
正式选型须再固定具体发布版本。承接[既有渲染选型](render_pipeline_options.md)，
但本次基线已是 `18647dc` 的 GPU YUV 显示通路，不能沿用改造前必经 CPU 下载/转色的假设。

后续已完成 Qt 前后台分段诊断与独立 D3D11 架构实验，并定位/修正周期网卡查询占用 GUI。
实际数据及限制见[前后台呈交验证](../../validation/video/background_presentation.md)。
该实验没有使用 SDL，下面关于 SDL 的能力与线程契约仍属于文档/源码调查。

## 当前问题与判定边界

当前 Windows 运行已使用 `d3d11-shared-yuv`。用户日志北京时间 08:52:07 的最近一秒窗口：
解码最大 0.667ms、`renderWait` 最大 54.853ms、纹理准备最大 0.938ms、
准备完成至 Qt 呈交最大 0.862ms、首包至 Qt 呈交最大 59.942ms。
各最大值可能来自不同帧，不能直接相加。
`renderWait` 是 GUI 请求更新至 `updatePaintNode()` 进入的观测区间，
可能包括 GUI/渲染调度、场景图同步前准备及后端帧准备等待，不能全部认定为 GUI 执行时间。
日志尚未完整记录激活、暴露和遮挡状态，不能确诊切屏后由 Qt、系统合成器或驱动哪一层触发。
实测基线与尚未完成的硬件验收见[GPU 通路验证](../../validation/video/gpu_pipeline.md)。

## SDL2 与 SDL3 能改变什么

| 路线 | 已核实的能力/约束 | 对当前项目的判断 |
| --- | --- | --- |
| SDL2 Renderer | `SDL_RenderPresent()` 只能在 main thread 调用 | 适合简单视频窗口对照；替换调用并不能承诺 Qt GUI 阻塞时持续呈交 |
| SDL3 Renderer | `SDL_RenderPresent()`、`SDL_CreateTextureWithProperties()` 同样要求 main thread；提供原生纹理包装属性 | 若做新的 SDL 实验优先考察 SDL3，但仍须处理硬件帧、设备和窗口集成 |
| SDL3 GPU API | 命令录制可并行；window claim 和 swapchain acquire 限定窗口创建线程，创建窗口本身要求 main thread | 不能当作任意工作线程呈交的通用捷径；需要按窗口线程契约重新设计 |
| 自有原生视频交换链 | 应用掌握视频唤醒、取最新帧、GPU 命令、呈交与在途帧数 | 最直接验证 Qt 场景图调度依赖；不是只替换库名称 |

线程契约来源：[SDL2 RenderPresent](https://wiki.libsdl.org/SDL2/SDL_RenderPresent)、
[SDL3 RenderPresent](https://wiki.libsdl.org/SDL3/SDL_RenderPresent)、
[SDL3 CreateTextureWithProperties](https://wiki.libsdl.org/SDL3/SDL_CreateTextureWithProperties)、
[SDL3 CreateWindow](https://wiki.libsdl.org/SDL3/SDL_CreateWindow)、
[SDL3 ClaimWindowForGPUDevice](https://wiki.libsdl.org/SDL3/SDL_ClaimWindowForGPUDevice)、
[SDL3 AcquireGPUSwapchainTexture](https://wiki.libsdl.org/SDL3/SDL_AcquireGPUSwapchainTexture)、
[GPU API 概览](https://wiki.libsdl.org/SDL3/CategoryGPU)。

### 保留硬件帧通路的条件

SDL3 的 D3D11 Renderer 接受 `SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`，
用于包裹已有 `ID3D11Texture2D`。固定版本源码还包含 NV12/P010 转换与 SRV 创建。
但该代码创建的是 `D3D11_SRV_DIMENSION_TEXTURE2D`，没有在此创建路径选择 texture array slice；
FFmpeg D3D11VA 的解码数组和 slice 编号不能直接等同于普通采样纹理。
资源必须能在 renderer 的设备中访问，另需满足格式/BindFlags、同步和解码池生命周期约束。
可行设计仍可能需要把源 slice 复制到兼容纹理，跨设备时先共享；应保留 GPU 内复制，
不能退回 `SDL_UpdateYUVTexture()` 上传 CPU 像素后声称延续了当前硬件直通。
前述复制与同步要求为结合源码及当前 FFmpeg 帧契约的工程推论。
来源：[SDL3 纹理属性](https://wiki.libsdl.org/SDL3/SDL_CreateTextureWithProperties)、
[固定版本 D3D11 renderer 源码](https://github.com/libsdl-org/SDL/blob/release-3.2.0/src/render/direct3d11/SDL_render_d3d11.c)。

SDL3 GPU API 的设备文档列出 Vulkan、D3D12、Metal，未提供 D3D11 GPU backend。
所以 SDL3 Renderer 的 D3D11 原生纹理包装能力不能直接套到 SDL3 GPU API 上。
对目前 D3D11VA 输入，改用 SDL3 GPU API 还需跨 API 互操作设计；
已查固定版本 `SDL_gpu.h` 不应当作具备任意 FFmpeg 原生纹理导入保证。
来源：[CreateGPUDevice](https://wiki.libsdl.org/SDL3/SDL_CreateGPUDevice)、
[固定版本 GPU 头文件](https://github.com/libsdl-org/SDL/blob/release-3.2.0/include/SDL3/SDL_gpu.h)。

Ubuntu 也需重新验证 VAAPI/DRM PRIME、EGL/Vulkan、modifier、fence 和设备关系，
SDL 支持 Linux 不等于任意 VAAPI surface 可自动导入。
Intel 核显与 NVIDIA 都按实际 API、HEVC 格式和驱动能力验证，不按厂商名预设独显条件；
混合显卡跨适配器仍可能要求回退。

## 仍保留 Qt 的优化路线

1. **先定位调度来源。** 补齐窗口激活、暴露、最小化/可见状态和关键渲染阶段时间，
   记录已解码帧覆盖数、呈交间隔和输入节拍。前台、失焦但可见、完全遮挡、最小化分别测试，
   用 GPUView/PresentMon 对照 CPU/GPU 等待；故障区间不能仅凭一个 `renderWait` 分段确定根因。
   参考：[Qt 场景图渲染循环](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)、
   [GPUView](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-gpuview)、
   [PresentMon](https://github.com/GameTechDev/PresentMon)。
2. **做有限的 Qt 调度对照。** 当前已经 threaded、关闭同步，并非重新设置这两个选项就算新优化。
   官方说明 `QWindow::requestUpdate()` 在多数平台由 GUI 定时器驱动，低层基准可用
   `QT_QPA_UPDATE_IDLE_TIME=0` 探索减少空闲时间；这是实验变量，不保证后台长尾消失。
   同时对照 Release、关闭无关 QML 动效/高频状态更新，并记录 CPU/功耗代价。
   来源：[Qt 场景图的垂直同步与 requestUpdate 说明](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)。
3. **沿用当前 GPU 像素桥，试验独立视频呈交。** 先不加 HUD，视频线程通过有界最新帧邮箱唤醒，
   Windows 使用 D3D11 交换链，Ubuntu 单独实现并验证 EGL/Vulkan 呈交与 VAAPI 互操作。
   主动阻塞 GUI 200ms/1s，对照视频帧号和呈交是否继续推进；这比整套 UI 重写更能隔离变量。
   Windows 无 HUD 实验现已验证 GUI 停顿隔离，详见本文开头的实测报告；
   Ubuntu 呈交与完整产品集成仍未实现，不是跨平台产品成功保证。
4. **视频独立后再合成 QML。** 原生视频子窗口不是普通 QML Item。
   Qt Quick `WindowContainer` 文档明确更高 z 的 sibling 和普通 child Item 不会自动覆盖嵌入窗口，
   需专门 child window 等设计，必须验证裁剪、DPI、弹窗、输入和 X11/Wayland 行为。
   另一选择为 `QQuickRenderControl` 离屏生成 HUD；其 `sync()` 仍要求阻塞 GUI，
   若每张视频帧都等待一次 HUD 同步，又会恢复依赖。视频应能复用上一张已完成的 HUD 纹理，
   只有 HUD 变化时才更新，并明确纹理所有权和 GPU 完成信号。
   来源：[WindowContainer](https://doc.qt.io/qt-6/qml-qtquick-windowcontainer.html)、
   [QQuickRenderControl](https://doc.qt.io/qt-6/qquickrendercontrol.html)。

Windows waitable swapchain 能让应用在合适时机开始下一帧，配合限制在途帧数减少排队。
`GetFrameLatencyWaitableObject()` 要求创建时使用相应 flag，`SetMaximumFrameLatency()`
在此交换链上的默认值本来就是 1；不能把“设成 1”作为无条件新收益，也不能说它保证呈交不等待。
本项目当前最大成本在进入场景图前的观测区间，需看底层时间线再判断交换链调整收益。
来源：[GetFrameLatencyWaitableObject](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)、
[SetMaximumFrameLatency](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency)。

Windows 11 官方说明完全遮挡/最小化等情况下，不保证保留高分辨率计时行为。
项目已有 `BackgroundTimerResolution` RAII，显式关闭
`PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`，但目前实例仅位于
`MqttWorker::begin()`，独立视频接收生命周期没有同样的守卫。
本次日志 MQTT 为 offline、会话未 active，视频仍可独立运行，因此不能假设这次视频已启用该策略。
建议核实实际策略，按引用计数把保护覆盖到独立视频生命周期，再做前后台受控 A/B；
这属于已有保护范围的补齐，尚不能证明就是当前几十毫秒长尾的根因。
窗口失焦时是否涉及其他省电/驱动策略仍需观测。
来源：[timeBeginPeriod](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)。

## 建议

不建议因 SDL 的 API 更薄就直接替换完整界面。本轮已记录后台状态并完成 Qt 小范围对照，
以及保留 FFmpeg/GPU 帧的 Windows 无 HUD 独立交换链实验。
若需要一个独立、轻量的视频窗口或进程作为基准，SDL3 比新增 SDL2 路线更适合评估，
但收益来自呈交循环和 GUI 工作负载被分离，不能归因为 SDL 名称本身。
若需要成熟的视频缩放/色彩渲染，可继续评估既有选型中的 libplacebo；
它也不自动解决窗口线程、GPU 互操作和 QML 合成问题。

所有路线都须分别验 Windows/Ubuntu、Intel 核显/NVIDIA、混合显卡、失焦可见/遮挡/最小化，
同时比较端到端延迟、实际呈交间隔、覆盖丢帧、CPU、功耗和持续运行。
隐藏窗口可以按产品策略暂停绘制并保持接收/最新帧，恢复时立即显示最新画面；
不能把后台暂停期间的 low 当作前台流畅度，也不能通过清空统计隐藏前台卡顿。
