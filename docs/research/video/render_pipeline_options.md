# 视频渲染管线优化与替代方案

日期：2026-11-15。研究对象为 Qt Quick 6.12、FFmpeg HEVC 解码与 Windows/Ubuntu 双平台。
本记录区分官方 API 契约、第三方指定源码快照和项目设计推论；没有运行候选渲染器或测量其收益。
此前 Qt 更新请求、DXGI 唤醒与帧队列的研究见 [Qt 渲染等待](qt_render_wait.md)。

## 当前代码与会话证据

当前硬解显示链路为：

```text
UDP 重组 → 有界压缩 AU 队列 → FFmpeg D3D11VA/VAAPI 硬件帧
       → av_hwframe_transfer_data 下载 CPU
       → NV12 拆为 YUV420P → swscale 转 RGBA QImage
       → 最新帧邮箱 → GUI poll/refresh → QQuickItem::update
       → Qt 渲染同步 → 每帧 createTextureFromImage → GPU 显示
```

下载见 `src/video/hevc_decoder.cpp` 的硬件帧分支，NV12 拆平面和 RGBA 分配/转换见
`src/video/video_color_converter.cpp`；GUI 交接与纹理创建见 `src/video/video_view.cpp`。
目前 `VideoFrame` 的显示有效性绑定到 RGBA `QImage`，不是可直接交给 GPU 的硬件帧。
所以仅替换 `VideoView` 的绘图 API，不会自动删除前面的下载和转色。
这些是当前源码事实，非对所有 Qt 视频应用的描述。

2026-11-15 的 `71c9f45` 会话（22:54:02—22:59:09，Qt 6.12 / Debug / D3D11）
记录到两次溢出，协议 AU 编号分别为 10942、11007，共清除 6 个待处理 AU。
溢出前在途 decode 已运行 26.615/17.233ms，最老待处理 AU 已等待 55.741/52.482ms；
最近入队批号各不相同，触发批截至当时只有 1 个完整 AU，未显示同批密集交付的证据。
第一次溢出前已完成的上一帧总解码 21.167ms，其中下载 11.337ms、转色 8.956ms、
codec 0.651ms。耗时是同步 CPU 侧墙钟观测，包含驱动等待、内存访问和线程被抢占，
不能把它们等同于 GPU/CPU 独占执行时间。

同一会话的 GUI 心跳超时最大 847.622ms；首包读出至 Qt 呈交最大 194.931ms，
对应一秒窗口的 renderWait 最大 182.476ms。视频清屏后无法继续生成正常呈现样本，
因此“最大呈现延迟 194.931ms”不代表无画面期间只停了这么久。
来源：用户提供日志中第 256–258、268、280–283 行的剪贴板告警、恢复事件与快照。
机器专用路径与原始日志不纳入版本管理。

Qt 6.12 的 Windows 剪贴板 `retrieveDataObject()` 在 `OleGetClipboard()` 失败后
最多尝试 3 次，每次失败记录告警并 `QThread::msleep(50)`。
用户两条 `Retrying to obtain clipboard.` 都发生在 GUI 线程；按该实现意味着两次各 50ms
的休眠，解释了约 100ms 的确定性阻塞，但不足以解释全部 847.622ms 心跳超时。
这属于 Qt 平台剪贴板实现与外部占用的交互，不能仅通过替换 D3D11 为 Vulkan 消除。
来源：[Qt 6.12 Windows clipboard 源码](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/windows/qwindowsclipboard.cpp#L79-L97)。

FFmpeg 7.1 的 D3D11 下载实现会锁 device context，将解码纹理复制到 staging texture，
再 `Map(D3D11_MAP_READ)` 并复制到软件平面。这里可能等待 GPU/驱动，且没有调用
Qt 场景图。因此 Qt 显示、截图与解码可以因共享 GPU/系统调度互相影响，但日志尚不能
证明 Qt 渲染架构是下载、转色、GUI 停顿三者的唯一共同原因。
来源：[FFmpeg 7.1 D3D11 下载实现](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavutil/hwcontext_d3d11va.c)。

## 现有架构内优先考虑的优化

### 1. 保留 YUV 平面，交给 GPU 转色

```text
软件解码输出 / 硬件帧下载 → NV12 或 YUV420P 不可变平面
                           → 持久化 Y/UV 或 Y/U/V 纹理
                           → GPU shader 转色、缩放并与 QML 合成
```

此路线可以删除当前 CPU NV12 拆平面和 swscale RGBA 转色，软件解码无需硬件下载；
硬解若仍使用软件平面则继续支付下载成本。保留当前 QML/HUD、场景图和 GUI 同步点。
1080p、8bit、无行填充时，NV12 每帧约 3.11MB，RGBA 约 8.29MB；60fps 仅像素上传量
分别约 187MB/s 与 498MB/s。减少的是理论上传字节数，不能据此声称延迟等比例下降。

Qt 的 `QSGMaterialShader` 支持多个 sampled image 和跨图形 API shader 包；可用两个
采样器处理 NV12，三个处理 YUV420P，构建时通过 Qt Shader Tools 的 qsb 打包。
较低层的平面纹理上传可采用 R8/RG8 等 QRhi 格式，需要接受 Qt::GuiPrivate 和版本耦合。
来源：[QSGMaterialShader](https://doc.qt.io/qt-6/qsgmaterialshader.html)、
[QRhiTexture 格式与更新](https://doc.qt.io/qt-6/qrhitexture.html)、
[QRhi 兼容与线程限制](https://doc.qt.io/qt-6/qrhi.html)。

应复用有限纹理和上传资源；“每帧更新纹理内容”不等于“每帧新建/销毁纹理”。
当前 `createTextureFromImage()` 的 CPU 计时不能反映最终驱动分配和上传成本，
是否存在明显分配抖动仍需 GPU/驱动时间线确认。
颜色转换须保留矩阵、全/有限范围、采样位置、位深、像素宽高比、裁剪与方向等信息，
用现有转色参考图回归，避免只在默认 NV12/BT.709 上看起来正常。

这是跨 Windows/Ubuntu 的基础路线，也便于把“CPU 转色”和“Qt 调度”等因素分别做 A/B。
它不是 GUI 停顿隔离方案。

### 2. 硬件帧留在 GPU：允许有界 GPU 内复制

```text
FFmpeg 硬件帧 → GPU 资源桥 / 必要时 GPU 内复制
              → GPU YUV 转色 / video processor → Qt 场景图合成
```

这是针对当前下载和转色路径的长期首选候选。目标是取消每帧 GPU→CPU→GPU 往返，
无需强求完全没有 GPU 内复制。先用兼容纹理桥验证收益，比直接承诺零复制更可执行。

Windows D3D11VA 帧涉及 `ID3D11Texture2D` 和纹理数组 slice；解码池输出的 texture array
带 `D3D11_BIND_DECODER` 时，Microsoft 明确限制其用于 `CreateShaderResourceView`。
因此不能假定解码器给出的每个 surface 都能直接采样。
Qt `QSGD3D11Texture::fromNative()` 官方仅适合 2D RGBA，不是 NV12/数组 slice 的通用导入器。
可先将 decoder slice 复制到适合采样的 GPU 资源，做 YUV shader 或 video processor 转换，
得到 Qt 可合成的纹理；再评估是否值得进一步省去中间复制/离屏 pass。
来源：[FFmpeg D3D11 帧契约](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavutil/hwcontext_d3d11va.h)、
[D3D11 bind flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_bind_flag)、
[Qt D3D11 纹理导入](https://doc.qt.io/qt-6/qnativeinterface-qsgd3d11texture.html)。

同一个 GPU 名称不代表同一个 device，更不代表同一纹理可安全跨设备使用。
可通过 `QQuickGraphicsDevice` 控制/引用既有设备，或者在同适配器上用共享纹理桥连接
独立设备；两者都必须处理上下文并发与同步。Qt 6.12 Multimedia 自己的 D3D11 converter
就使用共享纹理、GPU 内复制及 keyed mutex 连接 FFmpeg 设备和 RHI 设备，可作为设计参考，
但它是私有实现，不能直接依赖内部类，也不能照搬无限等待到实时视频路径。
来源：[QQuickGraphicsDevice](https://doc.qt.io/qt-6/qquickgraphicsdevice.html)、
[Qt 6.12 D3D11 texture bridge](https://github.com/qt/qtmultimedia/blob/v6.12.0/src/plugins/multimedia/ffmpeg/qffmpeghwaccel_d3d11.cpp)。

Ubuntu 对应评估 VAAPI surface→DRM PRIME/dma-buf→EGLImage 或 Vulkan import，
必须探测格式、modifier、同步和显卡匹配。Qt 6.12 的 VAAPI texture converter 有 surface
导出和平面描述实现可参考，但 NVIDIA/Linux 的解码与互操作能力不能用 VAAPI 路线一概承诺。
来源：[Qt 6.12 VAAPI converter](https://github.com/qt/qtmultimedia/blob/v6.12.0/src/plugins/multimedia/ffmpeg/qffmpeghwaccel_vaapi.cpp)、
[FFmpeg 7.1 VAAPI 导出实现](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavutil/hwcontext_vaapi.c)。

跨平台设计应共享“不可变视频帧、颜色和时间元数据、资源租约”契约，平台句柄留在适配层。
硬件帧需持引用直到 GPU 真正结束采样；仅看到 `frameSwapped` 或提交复制命令不能立即释放。
在有界资源池中使用完成同步，设备丢失、解码重置、场景图销毁、尺寸变化时丢弃旧代资源。
共享 device/context 需遵守 D3D11 串行化，不能为省复制把驱动等待搬入 GUI 同步临界区。

### 3. 在既有 Qt loop 内改善取帧时机与 GUI 工作

当前 GUI `refresh()` 固定一张 QImage，后续等待场景图同步时即使邮箱更新也可能仍画旧快照。
可在 GUI 仅发合并更新请求，在同步或受控 preprocess 阶段从线程安全邮箱取最新已解码帧。
这能改善“画面新鲜度”，不能让尚未发生的渲染提前，也不能使 GUI 卡住时仍稳定启动新帧。
必须同时记录显示率和已解码覆盖数量，防止只换更新的帧让耗时指标变小、实际吞吐却下降。
来源：[Qt scene graph 同步/preprocess](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)。

减少逐帧无意义几何通知和重布局、避免在视频通知路径同步读剪贴板、用 GUI 线程栈查明
未解释的停顿，都值得做受控定位。但不能擅自从工作线程调用 QQuickItem 或把 QClipboard
移到后台线程；窗口系统和 Qt 对象线程契约仍必须遵守。

继续沿用已验证的 threaded loop；项目此前 basic loop 对照更慢，不能把删除渲染线程当优化。
窗口更新唤醒、beginFrame、同步和呈交等待应分别测量；现有私有 Qt 调度开关只作版本固定
的诊断 A/B，不作为未经验证的跨平台默认配置。关闭 vsync 也不会取消全部调度和系统合成。
来源：[现有实验](../../validation/video/latency_investigation.md)、[调度研究](qt_render_wait.md)。

### 4. Qt Multimedia / VideoOutput 是另一种保留 QML 的实现

可以把外部解码得到的软件 NV12/YUV420P 平面包装为 `QVideoFrame`，通过 `QVideoSink`
交给 `VideoOutput`，由 Qt 的视频渲染路径处理颜色和纹理。这是自写 YUV 材质的候选替代，
会增加 Qt Multimedia 模块和部署依赖，仍受 Qt Quick 场景图调度约束。
当前官方 `QVideoSink::setVideoFrame()` 明确声明线程安全，但这不保证 GUI 卡住时显示仍更新。
来源：[QVideoFrame](https://doc.qt.io/qt-6/qvideoframe.html)、
[QVideoSink](https://doc.qt.io/qt-6/qvideosink.html)、
[VideoOutput](https://doc.qt.io/qt-6/qml-qtmultimedia-videooutput.html)。

`QAbstractVideoBuffer` 公开接口提供 format/map/unmap，不能由 `RhiTextureHandle` 枚举
推断任意外部 FFmpeg 硬件帧都有现成稳定导入通道。Qt 自身硬件帧的 `mapTextures()`
适配在 `QHwVideoBuffer` / `QFFmpegVideoBuffer` 等私有类中。
若应用强制 map 硬件帧或转 QImage，还可能重新触发下载。若要换成 Qt 自带完整解码器，
还需评估自定义 UDP AU、无容器输入、恢复代数与精确诊断能否继续保持；不是换个 QML
VideoOutput 就完成整条媒体链重构。
来源：[公开 buffer 头文件](https://github.com/qt/qtmultimedia/blob/v6.12.0/src/multimedia/video/qabstractvideobuffer.h)、
[Qt 私有硬件 buffer](https://github.com/qt/qtmultimedia/blob/v6.12.0/src/plugins/multimedia/ffmpeg/qffmpegvideobuffer_p.h)、
[硬件 buffer map 实现](https://github.com/qt/qtmultimedia/blob/v6.12.0/src/plugins/multimedia/ffmpeg/qffmpegvideobuffer.cpp)。

## 更换管线要分别解决什么

以下三个目标应独立评价，不能把“换成 Vulkan”“换成 SDL”当成同时完成：

1. 去掉硬件帧回到 CPU、CPU 转 RGBA、重新上传 GPU 的往返。
2. 缩短更新请求、线程同步、交换链呈交的等待和在途帧队列。
3. GUI 长时间无响应时，让视频仍能独立显示最新帧。

原生纹理互操作主要针对目标 1；独立视频交换链和调度主要针对目标 2、3。
共享 GPU、驱动或操作系统调度问题仍可影响独立渲染器，窗口分离不等于物理资源隔离。
以下“可移除下载”均指满足互操作条件后无需把每帧像素搬回 CPU，不承诺没有 GPU 内复制或同步。

## 保留 Qt Quick 的边界

### QQuickRhiItem 适合跨后端自定义绘制，但仍属于 Qt Quick 调度

`QQuickRhiItem` 自 Qt 6.7 提供，使用 QRhi 实现后可随 Qt 选择的图形 API 运行。
它通常将应用绘制结果写入独立 color buffer，作为纹理交给 Qt Quick 合成。
item 和 renderer 通信的 `synchronize()` 在渲染线程执行，此时 GUI 线程被阻塞。
因此，把 `VideoView` 改为 `QQuickRhiItem` 可以承载 YUV shader 或硬件纹理适配，
但本身不会解除 GUI 同步依赖，并且需评估额外离屏渲染/合成。
来源：[QQuickRhiItem 官方说明](https://doc.qt.io/qt-6/qquickrhiitem.html)。

`QQuickRhiItem` 是公开 API，但其使用的 QRhi 家族只有有限兼容保证，官方明确不提供
这些类的源码或二进制兼容保证，并要求使用 Qt GuiPrivate 头文件。
当前项目固定 Qt 6.12 时可管理此成本，升级 Qt 后必须重新编译并回归所有图形后端。
不能将“公开 QQuickRhiItem”推广成“全部 QRhi 用法长期 ABI 稳定”。来源同上。

`QQuickFramebufferObject` 绑定 OpenGL/OpenGL ES；若同时要求 Windows D3D11 与
Ubuntu，不能把它作为保持所有后端通用的替代。来源同上。

### 切换 Qt RHI 后端适合受控对照

Qt Quick 的硬件场景图可使用 OpenGL、Vulkan、Metal、Direct3D 等后端；
threaded render loop 的 GUI/scene graph 同步机制并不因换后端而消失。
改变呈交后端也不会自动改变 FFmpeg 的解码输出格式或删除应用自己的下载、转色代码。
来源：[Qt 场景图与渲染循环](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)。

对当前项目的推论：D3D11→Vulkan 适合检验特定驱动或交换链路径，
但应先核对解码端仍为 D3D11VA 时是否引入新的跨 API 互操作。
不能先换 API，再继续下载 RGBA，并期待由此自然获得 GPU 直显。

## 路线一：保留 QML，视频使用独立原生窗口和交换链

### Windows 侧可选结构

```text
接收/组帧 → FFmpeg D3D11VA → 有界硬件帧邮箱
                            → 独立视频渲染线程
                            → GPU YUV 转色/缩放 → DXGI swapchain
Qt GUI/QML → 控制与 HUD（单独窗口或单独合成层）
```

应用自己控制交换链和呈交线程后，可让视频帧的消费不再依赖每次 QML 更新。
这是本项目的设计推论，需要真正保证视频线程不等待 GUI 的每帧握手；
仅把窗口封装成 Qt item 并仍从 GUI 定时器逐帧驱动，不能获得这种隔离。

Windows DXGI 有 `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` 和
`GetFrameLatencyWaitableObject()`，官方要求每帧开始渲染前等待该对象，包括第一帧。
`SetMaximumFrameLatency()` 仅可用于带上述 flag 的交换链，限制可排队的 back buffer 帧数。
它给应用明确的排队控制，不是“不需要等待”的接口，也不保证在 Qt 外就更低延迟。
来源：[waitable object](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)、
[maximum frame latency](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency)。

Microsoft 推荐现代应用评估 flip model；Independent Flip、MPO 等优化能否生效依赖
窗口覆盖、尺寸、缩放和硬件能力。它们不是任意子窗口都能获得的保证。
撕裂 flag、无同步呈交与 waitable queue 需要分别做 A/B，不能只比较一个内部等待指标。
来源：[DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)。

若需要将视频和 HUD 分层交给系统合成，可考虑 DirectComposition。
`CreateSwapChainForComposition()` 创建的交换链可以作为 `IDCompositionVisual::SetContent()`
的内容；其公开契约要求 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` 和 `DXGI_SCALING_STRETCH`。
这与普通 HWND 交换链是不同集成方案，不应混用创建参数。
来源：[composition swapchain](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)。

线程安全仍需设计：D3D11 device 对象支持多线程创建资源，但 immediate device context
不是可任意并发使用的对象。解码与渲染复用同一设备/上下文时必须串行化相关调用，
或设计明确的设备与资源同步边界；否则“独立线程”会引入数据竞争。
来源：[Microsoft D3D11 多线程简介](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)。

### Qt 内嵌与 HUD 的代价

Qt Quick `WindowContainer` 自 Qt 6.8 提供，可管理嵌入 `QWindow` 的位置、尺寸和层次。
官方明确：嵌入的 native window 在 Qt Quick 场景之上，普通更高 z 值的 sibling item
不会自动覆盖它；文档的绕行方案是把需要覆盖的内容放进另一个专门子窗口。
因此“保留现有所有 QML HUD，再随手嵌入原生视频窗”不是无代价替换。
需验收透明度、裁剪、弹窗、输入焦点、全屏、缩放、DPI、移动跨屏和截图行为。
来源：[WindowContainer](https://doc.qt.io/qt-6/qml-qtquick-windowcontainer.html)。

### Ubuntu 侧必须独立实现与验收

同等目标可以采用 EGL/OpenGL 或 Vulkan 原生视频显示，保留同一份 AU 接收、解码和帧元数据契约。
VAAPI/DRM PRIME 帧可评估 dma-buf 导出/导入，不能照搬 D3D11 纹理句柄。
`EGL_EXT_image_dma_buf_import` 定义了以文件描述符、fourcc、平面 offset/pitch
创建 EGLImage 的接口；实际格式、modifier 与同步能力仍需结合驱动探测。
来源：[Khronos EGL 扩展原文](https://github.com/KhronosGroup/EGL-Registry/blob/main/extensions/EXT/EGL_EXT_image_dma_buf_import.txt)。

Vulkan 的 FIFO、MAILBOX、IMMEDIATE 是不同呈交模式；MAILBOX 仍等待垂直空白但替换
待显示图像，IMMEDIATE 可产生撕裂。必须查询当前 surface 支持的模式。
即便应用不使用 Qt Quick 的渲染循环，Wayland/X11、桌面合成器和驱动仍参与呈现。
Wayland 客户端不能按 Windows HWND 的假设任意操作窗口层次。
来源：[Khronos WSI 规范](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/VK_KHR_surface/wsi.adoc)、
[Wayland 核心协议](https://wayland.freedesktop.org/docs/html/apa.html)。

工程评价：对“GUI 卡住时视频仍更新”最直接，平台适配与 HUD 集成成本也最高之一。
可先做没有 HUD 的独立视频窗实验，确认性能与停顿隔离的收益，再决定是否接入完整界面。

## 路线二：应用拥有显示管线，QML 作为离屏 HUD

```text
FFmpeg 硬件帧 → 应用原生视频渲染 → 应用交换链 → 屏幕
                                     ↑
QML GUI → QQuickRenderControl → 离屏 HUD 纹理
```

`QQuickRenderControl` 的官方用途就是把 Qt Quick 渲染到应用指定的 render target，
由应用负责驱动 polish、sync、render 及外部合成。Qt 6 的非 OpenGL 后端还要求正确调用
`beginFrame()` / `endFrame()`。`QQuickGraphicsDevice::fromDeviceAndContext()`
可引用已有的 D3D11 device/context；Vulkan 有 `fromDeviceObjects()`。
这些对象不替调用方承担原生资源生命周期。
来源：[QQuickRenderControl](https://doc.qt.io/qt-6/qquickrendercontrol.html)、
[QQuickGraphicsDevice](https://doc.qt.io/qt-6/qquickgraphicsdevice.html)。

关键限制：`prepareThread()` 能准备在 GUI 线程外执行同步和渲染，
但官方仍要求 `sync()` 期间 GUI 线程被阻塞。RenderControl 不是自动消除 GUI 依赖的开关。
应用可进一步设计为“视频按自己节奏呈交，GUI 可用时更新 HUD，GUI 忙时复用上一次 HUD 纹理”，
这能否隔离停顿取决于线程、资源与同步设计，是应用策略而非 Qt 的现成保证。
若每个视频帧都无条件等待 `polishItems()`/`sync()`，仍然会把 GUI 长停顿传递给视频。
来源：[RenderControl 线程和 sync 契约](https://doc.qt.io/qt-6/qquickrendercontrol.html#sync)。

工程评价：可保留 QML HUD，同时获得视频主时钟、交换链和纹理通路的控制权；
但调度、输入事件映射、窗口尺寸/DPI、帧边界、失效重建与退出顺序都由项目承担。
独立视频窗证明“绕过 Qt Quick 视频调度”确有必要之后，再做此路线比直接全量重构更稳妥。

## 路线三：使用现成渲染或媒体库

### SDL3：适合轻量显示实验，不自动提供 GUI 停顿隔离

SDL3 renderer 可对 YUV 做显示转换，`SDL_CreateTextureWithProperties()`
支持包裹已有 D3D11 或 OpenGL 纹理，包含相应 YUV 平面的原生属性。
但该函数官方明确只允许在 main thread 调用，且纹理必须匹配选定 renderer 的 API。
因此不能仅凭换成 SDL_Renderer 就承诺在 Qt GUI 线程被剪贴板阻塞时视频仍持续运行。
来源：[SDL3 原生纹理创建属性与线程契约](https://wiki.libsdl.org/SDL3/SDL_CreateTextureWithProperties)。

若用 SDL 做单独视频进程，可以获得进程级 GUI 事件循环隔离；代价是控制/帧资源 IPC、
窗口与 HUD 组合、输入和会话生命周期管理。若仍要由项目自己写 D3D11/Vulkan 渲染线程，
SDL 只负责窗口/事件，则真正的优化来自自有原生管线，不能归因于 SDL_Renderer。
FFmpeg 的硬件帧纹理数组 slice、设备归属与同步仍必须适配，不能把任意纹理指针视作可直接显示。
这些为基于上述 API 的工程推论。

### GStreamer：适合替换整段媒体管线，移植成本大于仅换 shader

当前接收重组输出的 HEVC AU 可经 `appsrc` 注入 GStreamer；
它有 `max-buffers`、`max-bytes`、`max-time` 等队列控制，应用仍需明确配置低延迟和时间戳。
Windows 的 `d3d11h265dec` 与 `d3d11videosink` 可协商 `memory:D3D11Memory`，
后者接收 NV12 等 GPU 格式并提供 VideoOverlay 接口。
所查 Ubuntu `vah265dec` 页声明 VAMemory 和普通 raw 输出；dma-buf 导出能力需要另行
选择与验证相应插件/版本，sink 也需按显示环境选择，不能默认该 decoder 已协商 DMABuf。
来源：[appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html)、
[d3d11h265dec](https://gstreamer.freedesktop.org/documentation/d3d11/d3d11h265dec.html)、
[d3d11videosink](https://gstreamer.freedesktop.org/documentation/d3d11/d3d11videosink.html)、
[vah265dec](https://gstreamer.freedesktop.org/documentation/va/vah265dec.html)。

若保留 FFmpeg 再把它的硬件帧塞进 GStreamer，并不自动共享 GStreamer 的设备和 allocator，
互操作仍是项目工作。若连解码一起换成 GStreamer，则需重新实现现有代数恢复、首包时钟、
输入/输出帧编号关联、低延迟队列和诊断契约，并增加两平台插件部署矩阵。
这属于媒体链重构，而非单个渲染组件替换。

`qml6glsink` 仍向 Qt Quick 场景图提供 OpenGL 纹理；官方特别警告 Qt 与 GStreamer 的
GLDisplay/context 不匹配可能导致启动失败、拒绝 buffer，或每帧 GPU→CPU→GPU 往返。
它要求共享 Qt 提供的显示连接，并建议 qml6glsink 先于其他 GL 元件进入 READY。
因此它既不能直接替代当前 D3D11 Qt Quick 路径，也不能保证规避 GUI 渲染同步。
来源：[qml6glsink](https://gstreamer.freedesktop.org/documentation/qml6/qml6glsink.html)。

### libplacebo：较贴合“保留 FFmpeg，更换视频合成器”

libplacebo 提供视频 shader/色彩转换、渲染器和 D3D11/OpenGL/Vulkan 后端。
其 D3D11 API 可包装已有 device、swapchain 与外部纹理；`pl_d3d11_wrap()` 支持
texture array 的 `array_slice`，以及 NV12/P010 等视频资源的平面 shader view 描述。
必须同一 D3D11 device、`D3D11_USAGE_DEFAULT`，格式或资源 flag 不兼容时包装会失败。
来源：[libplacebo D3D11 头文件（固定快照）](https://github.com/haasn/libplacebo/blob/4385609bba1e939f194a288826f0c4b35a504f33/src/include/libplacebo/d3d11.h)。

不能因为它提供 `pl_map_avframe_ex()` 就认定任何 FFmpeg 硬件帧自动直通：
核查该快照的 libav helper，其硬件映射分支包括 DRM PRIME、VAAPI、Vulkan，
没有 `AV_PIX_FMT_D3D11` 分支。当前 Windows D3D11VA 路径应评估自行包装平面和 slice，
或采用同 GPU 内复制；Linux 的 VAAPI/DRM PRIME 则仍取决于 dma-buf import 能力。
来源：[libav helper 实现（固定快照）](https://github.com/haasn/libplacebo/blob/4385609bba1e939f194a288826f0c4b35a504f33/src/include/libplacebo/utils/libav_internal.h)、
[libav helper 公共接口](https://github.com/haasn/libplacebo/blob/4385609bba1e939f194a288826f0c4b35a504f33/src/include/libplacebo/utils/libav.h)。

工程评价：若需要更完整的视频色彩、缩放和未来 HDR 支持，值得作为原生显示实验候选；
它不替项目完成窗口/QML 合成或消除 GUI 调度依赖。当前只显示 SDR 视频时，
少量自有 YUV shader 可能更简单；不能默认引入完整滤镜质量配置，否则反而增加 GPU 成本。
库及所启用依赖的许可证、shader 编译工具和二进制闭包需与平台发布一起核对。
libplacebo 本体采用 LGPL 2.1 或更新版本，来源：[许可证](https://github.com/haasn/libplacebo/blob/4385609bba1e939f194a288826f0c4b35a504f33/LICENSE)。

## 候选路线比较

表中收益均为设计能力，不是本项目已经测得的结果。

| 路线 | 可去掉 CPU 转色 | 可去掉每帧 CPU 下载 | GUI 卡顿时独立显示视频 | 迁移成本 |
| --- | --- | --- | --- | --- |
| Qt Quick 内平面 YUV shader | 是 | 否，若仍用 CPU 平面 | 否 | 中 |
| Qt Quick 内硬件纹理互操作 | 是 | 条件成立时可去掉 | 不自动具备 | 高 |
| QVideoFrame / VideoOutput | 是，按 YUV 输入 | 外部硬件帧需额外适配 | 不自动具备 | 软件平面中，硬件互操作高 |
| 只换 Qt RHI 后端 | 不自动改变 | 不自动改变 | 否 | 对照低，正式切换中至高 |
| QQuickRhiItem | 自行实现后可去掉 | 互操作后可去掉 | 否 | 中至高 |
| 独立原生视频窗/交换链 | 是 | 互操作后可去掉 | 可以设计实现 | 高，HUD/平台适配显著 |
| RenderControl 离屏 QML HUD + 自有视频交换链 | 是 | 互操作后可去掉 | 复用旧 HUD 且避免逐帧 GUI 等待后可实现 | 很高 |
| SDL3 Renderer | 是 | 需匹配 API 与资源 | main-thread 契约下不自动具备 | 中，隔离进程另计 |
| GStreamer 媒体管线 | 是 | GPU memory 协商成功时可去掉 | 取决于 sink、线程与 UI 集成 | 高，替换解码链时更高 |
| libplacebo + 自有显示管线 | 是 | 平台互操作后可去掉 | 取决于显示调度 | 高，但可保留 FFmpeg |

不建议优先把整个 UI 重写为另一套框架。先用独立视频输出验证可消除哪一种等待，
再决定是否值得承担 HUD 合成和双平台窗口系统的长期维护。

## 建议的决策与验证次序

1. 固定真实码流、窗口、刷新率、同步设置、后台负载，用 Release 与当前 Debug 做对照；
   CPU/GPU 解码、菜单开关、剪贴板复现分别运行，保留首包时钟和实际输出计数。
   在短录制中抓 GUI/解码线程栈及 GPU 时间线，定位 847ms 停顿里除剪贴板休眠以外的部分。
2. 建立软件平面 YUV→GPU shader 的跨平台基础路径，先验证删除 CPU 转色的效果；
   现有 RGBA 路径保留作颜色和性能对照，不为 CPU 快速短测就取消硬解支持。
3. 在同一帧契约下试做 D3D11VA GPU 资源桥，允许有界 GPU 内复制；Ubuntu 同步设计和
   验证 VAAPI/dma-buf 适配及软件回退。若消除了 CPU 下载/转色，却仍因 GUI 停顿冻结，
   这是进一步调整显示调度的直接依据。
4. 若产品硬要求“GUI 阻塞时视频也必须继续”，可以与上述工作并行做一个无 HUD 的独立
   视频交换链实验，不必先完成 Qt 内全部微优化。主动阻塞 GUI 200ms/1s，检查视频帧编号
   与呈交时间线是否继续推进，区分 Qt GUI 依赖和共享 GPU/系统阻塞。
5. 独立呈交验证有实际收益后，再决定使用原生视频层叠加独立 QML 窗口，还是自有交换链
   加 RenderControl 离屏 HUD。后者应让 GUI 忙时复用上一张已完成 HUD 纹理，不能每帧等待 GUI。

这些是待执行实验，不是本轮已实现功能。每步只改变一个变量，前后交替重复；
增加队列深度或任意丢压缩 P/B 帧会改变参考链和延迟，不能用来掩盖消费不足。
可以合并/覆盖已解码、未显示的旧帧，但要显式记录数量并安全释放 GPU 资源。

验收同时看首包至呈交的中位数/P95/P99、长尾停顿、输出帧率/帧间隔、覆盖/恢复计数、
CPU/GPU/内存/显存与长稳，不能只看某阶段计时变成 0。
硬件路径去掉下载后，应在测量边界中补上 GPU copy/转色和完成同步，避免将等待挪到
Qt `frameSwapped` 之后误称为物理显示更快。Qt 官方明确 frameSwapped 是已排入呈交，
真实亮屏仍需外部测量或经验证的显示时间戳。
来源：[QQuickWindow::frameSwapped](https://doc.qt.io/qt-6/qquickwindow.html#frameSwapped)。

Windows 可用 GPUView 的 ETW 时间线查看 CPU/GPU 提交和调度；PresentMon 可辅助观察呈现
节奏，须确认当前模式与指标定义，不能把任意 Present 返回当显示器亮屏时间。
来源：[Microsoft GPUView](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-gpuview)、
[PresentMon 官方项目](https://github.com/GameTechDev/PresentMon)。

双平台验收分别覆盖 Windows D3D11、Ubuntu 原生 X11/Wayland，Intel 核显、NVIDIA、
混合显卡和无硬件互操作路径。不能把 WSL/offscreen 的功能回归当作 Linux GPU 验收。
按项目现有构建和回归习惯进行实验，不在本轮调研中增加产品依赖或更改默认渲染策略。

## 尚未验证

- 没有候选管线的同机延迟、帧率、功耗或内存测量，不给出确定收益毫秒数。
- 没有 Windows Intel 核显、混合显卡纹理共享、Ubuntu VAAPI、Wayland/X11 或设备丢失实机验证。
- 未验证 SDL3 原生纹理属性与当前 FFmpeg 输出资源的直接兼容性。
- 未验证 DirectComposition 路线与当前 QML HUD、截图工具和输入焦点的集成。
- Qt 文档使用官方当前 Qt 6 页面；具体到 6.12 的实现仍应结合项目固定版本源码和实测。
- libplacebo 源码固定为上述 commit；GStreamer/SDL 文档为 2026-11-15 获取的官方当前页，
  选型时需再固定发布版本和部署依赖。
