# 开源客户端图传解码链路调研

调研日期：2026-11-13。以下为源码静态分析，没有运行外部项目、执行同机性能对比或验证其驱动兼容性。
结论限定于下列快照；README、注释、类名和计划文档不能代替实际调用路径。
本轮仅增加调研记录，不修改 PIP-Client 的视频实现。

## 1. 来源与结论概览

| 项目 | 分析快照（main） | 主图传实现 |
| --- | --- | --- |
| [SharkClient](https://github.com/JNU-SHARK/SharkClient) | `e3be0ee1109a4db80fb0647f8963bd3627b18560`，2026-05-21 | Rust 接收重组、独立解码线程、多后端；实际输出转为 MJPEG，由 WebView 显示 |
| [SharkDataSever](https://github.com/JNU-SHARK/SharkDataSever) | `fecef639c10b852a96ab3af4ad3fc4dd95f01271`，2025-12-02 | 模拟发送端：FFmpeg/libx265 转码后通过 UDP 发送 |
| [RMMock](https://github.com/stydxm/RMMock) | `03ea98b14632bf9352bec602ce89d1a708bab2f8`，2026-06-09 | 主要为 GoCV 读取、libx265 编码发送；附带 Python/ffplay 接收示例 |
| [Aurora](https://github.com/asterShining/Robomaster_Aurora_Client) | `2b42b5c9868b65ff896ffae0f1bed71fcf28225e`，2026-07-18 | Go 接收重组、同步门控、小队列、独立 ffplay/SDL 视频窗口 |
| [RM26_Client](https://github.com/ClearWei/RM26_Client) | `e291f2f65f5a5924509e4e2fe1b148ff9d4b24af`，2026-08-26 | Qt 接收与解码共用工作线程；FFmpeg 软件解码、BGRA/QImage、QWidget 绘制 |

不能据此认定任何项目在同样的 1080p60 输入、同一设备与同一统计口径下优于 PIP。
软件解码、GPU 色彩转换、GPU 显示和硬件视频解码是不同环节，不能相互替代。

## 2. SharkClient

### 实际链路

```text
UDP → 多帧重组 → 有界 HEVC 压缩队列（6 个访问单元）
    → 独立 Rust 解码线程 → SmartDecoder → CPU 内存中的 YUV420P
    → YUV 转 RGB、JPEG 编码 → 本地 HTTP MJPEG → WebView <img>
```

- 后端启动时把请求的输出格式统一为 `mjpeg`，并非默认将 HEVC 交给前端 WebCodecs。
  [输出选择](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/udp_bridge.rs#L298-L315)。
- 解码在独立线程中顺序取出压缩数据；输出后调用 `to_jpeg`，再发布给 MJPEG broadcaster。
  [解码及发布](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/udp_bridge.rs#L461-L550)。
- 当前 HEVC 转 MJPEG 路径使用 30 FPS 常量控制发布间隔，默认 JPEG quality 为 48；
  JPEG 编码默认把高度限制在 720，环境变量可取消限制。
  因毫秒取整及按到达时间判断，30 FPS 是目标上限配置，不能解读成稳定精确的 30 FPS。
  [发布常量](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/udp_bridge/mjpeg.rs#L17-L19)、
  [分辨率限制和 CPU 转色](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder.rs#L53-L178)。
- 前端存在消费该 HTTP 流的 `<img>`。文件开头所说的“原始 YUV 经 IPC 交给 WebGL”
  与当前发布路径不一致，不能用该注释证明当前实现是 YUV/WebGL 直显。
  [实际前端](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src/views/Dashboard.vue#L3502-L3512)。

### 解码后端

`SmartDecoder` 在启用 `internal-ffmpeg` 编译特性时优先内嵌 FFmpeg，否则尝试外部 FFmpeg，
最后回退 libde265；Cargo 默认特性为空。内嵌解码器自身允许回退 FFmpeg 软件解码，
并不是“内嵌初始化失败才意味着没有硬解”。
[选择实现](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder.rs#L208-L290)、
[编译特性](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/Cargo.toml#L18-L25)。

| 后端 | 实现细节 |
| --- | --- |
| 内嵌 FFmpeg | Windows 尝试 D3D11VA/DXVA2，Linux 尝试 VAAPI；设备创建、硬件像素格式回调、`avcodec_send_packet/receive_frame`；硬件帧下载后整理为 YUV420P |
| 外部 FFmpeg | Windows 候选 D3D11VA、DXVA2、QSV、CUDA、auto；Linux 候选 VAAPI、QSV、CUDA、auto；HEVC 写 stdin，Y4M 从 stdout 读回 |
| libde265 | 软件解码；创建最多 4 个 WPP worker，输出 YUV420P |

来源：
[内嵌初始化](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder_internal.rs#L36-L156)、
[下载及平台候选](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder_internal.rs#L283-L347)、
[外部候选与进程](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder/gpu.rs#L232-L418)、
[libde265 线程](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder/native.rs#L103-L120)。

### 缓存与边界

- 压缩队列满时清空积压并等待关键帧；关键帧允许重新起解码，避免丢掉某个参考帧后继续盲解后续帧。
  [队列与恢复](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/udp_bridge.rs#L1003-L1086)。
- MJPEG broadcaster 只保留最新 JPEG，使用条件变量通知消费者。
  但这不表示 HTTP/TCP、浏览器内部也没有缓存。
  [发布槽](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/udp_bridge/mjpeg.rs#L22-L82)。
- 外部 FFmpeg 的启动检测是枚举 `-hwaccels` 并等待进程短时间存活，
  不能证明特定码流已成功输出硬件帧；其 stdout reader 使用 `mpsc::channel()`，
  消费时再排空保最新，不能把该通道称为严格有界队列。
  [检测与通道](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder/gpu.rs#L250-L418)、
  [消费](https://github.com/JNU-SHARK/SharkClient/blob/e3be0ee1109a4db80fb0647f8963bd3627b18560/src-tauri/src/video_decoder/gpu.rs#L433-L470)。

**对 PIP 的意义：**值得参考压缩帧依赖保护及最新结果槽；不应为解决 1080p60 性能问题照搬
“HEVC 解码后再 JPEG 编码”的显示方案。它以额外转码、默认降分辨率和发布限帧换取 WebView 可用性。
后端含硬解实现，也仍然不是端到端 GPU 零拷贝。

## 3. Aurora

```text
UDP → Go 重组 → HEVC 同步门 → 2 帧 channel
    → writer goroutine → ffplay stdin → ffplay/SDL 视频窗口
独立的 Wails/Vue HUD 窗口覆盖在视频窗口上
```

- UDP 只保存当前帧的分片，帧号变化即切换，组帧超时为 50 ms。
  同帧分片排序不等于支持跨帧交错重组。
  [接收逻辑](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/network/udp_receiver.go#L350-L446)。
- 同步门初始只接受同一访问单元中含 VPS/SPS/PPS 和 IRAP 的可恢复帧，之后正常放行。
  [门控](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/hevc_sync_gate.go#L40-L85)。
- 队列容量 2，满时淘汰旧帧，写入前再丢弃排队超过 120 ms 的帧。
  这里丢弃的是压缩数据；门控已经同步后，任意丢掉参考帧存在破坏后续参考链的风险。
  该风险为源码推断，并非本轮实测发现花屏。
  [队列常量](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/native_player.go#L19-L30)、
  [入队](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/native_player.go#L289-L340)、
  [写入及过期判断](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/native_player.go#L430-L466)。

**当前启动配置只有软件模式。**虽然结构保留 `hwaccel` 字段，实际模式列表只有 `software`，
命令使用 `-threads 1`，没有选择硬件模式。
其他参数包括 `nobuffer`、`flush_packets`、`discardcorrupt`、`-framedrop`、`-sync ext`、
`-probesize 32`、`-analyzeduration 0`、`-vf setpts=0`；SDL 环境关闭渲染 VSync。
这些是调度与缓冲策略，不能保证稳定 60 FPS，也不能代替显示实测。
[启动参数及环境](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/native_player.go#L750-L807)。

### 特别注意 FPS 口径

`noteDecoderFrame` 在写管道前计数，`notePresentFrame` 在完整写入 ffplay stdin 成功后计数。
后者不是 FFmpeg 解码输出回调，更不是 SDL 的呈现回调；管道接收了数据并不代表屏幕显示了对应帧。
因此不能把它的 `presentFPS` 与 PIP 的 OUT 直接比较。
[调用点](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/native_player.go#L440-L466)、
[统计实现](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/native_player.go#L679-L722)。

**对 PIP 的意义：**适合拿 ffplay 做同源码流的播放器对照，隔离 Qt 显示路径；
不宜直接移植多窗口叠层方案。该项目包含大量 X11 窗口贴层处理，Windows/Ubuntu、
Wayland、焦点和 HUD 叠加需要另外验证。
[窗口层实现](https://github.com/asterShining/Robomaster_Aurora_Client/blob/2b42b5c9868b65ff896ffae0f1bed71fcf28225e/pkg/video/x11_window_layer.go)。

## 4. RM26_Client

```text
QUdpSocket → 重组、收完本批数据报 → 本批完整帧 vector
    → FFmpeg HEVC 软件解码 → sws_scale BGRA → QImage
    → queued signal → GUI 缩放 → QWidget/QPainter
```

- 接收器先脱离 QObject parent，再移到高优先级 QThread，GUI 通过 queued connection 收图。
  这不是在 GUI 线程直接解码，但收包与解码仍共用同一个工作线程。
  `onReadyRead` drain 完当前 UDP 后，立即在同一回调中解码本批完整帧；
  解码时不持重组 mutex，不等于解码期间可以继续运行该线程的新 UDP 回调。
  [线程归属](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/widgets/VideoBackgroundWidget.cpp#L75-L82)、
  [收包与解码调用](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/VideoReceiver.cpp#L258-L286)。
- HEVC 使用普通 `avcodec_find_decoder`，线程数限制为 1～6，使用 `FF_THREAD_SLICE`、
  `LOW_DELAY`、`FAST` 及 `skip_loop_filter=AVDISCARD_NONREF`；该解码器未安装硬件设备上下文，
  当前分析的主路径是软件解码。
  [初始化](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/HevcDecoder.cpp#L61-L111)。
- parser 和 codec 跨调用保留；调用 `avcodec_send_packet/receive_frame`，输出经 swscale
  直接写入 QImage 自有内存，避免先写中间 RGB buffer 再复制。
  [解码](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/HevcDecoder.cpp#L140-L284)、
  [转换](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/HevcDecoder.cpp#L383-L440)。
- GUI 保存最新 QImage，调用 `update()` 合并重绘，但没有在这条信号连接中看到逐帧 queued event
  的合并机制。GUI 卡住时可能先积压图像事件，不能因为 `update()` 可合并就断言整个通道不积压。
  [收图](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/widgets/VideoBackgroundWidget.cpp#L211-L248)。
- 绘制前用 `QImage::scaled(..., IgnoreAspectRatio, FastTransformation)` 缩放到控件大小，
  然后 `QPainter::drawImage`；这可能拉伸画面，不符合 PIP 必须保持比例的要求。
  [缩放](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/widgets/VideoBackgroundWidget.cpp#L401-L425)。
- 呈现统计记录 `paintEvent` 中 `drawImage` 后的时间，仍不是显示器实际扫描完成。
  [绘制及统计](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/widgets/VideoBackgroundWidget.cpp#L264-L331)。

**对 PIP 的意义：**可参考按组帧、解码、GUI 绘制分阶段记录耗时。
不能仅看到它启用 slice 多线程，就恢复 PIP 已因实流挂起而禁用的 HEVC slice/WPP 并行。
其 QWidget/CPU 缩放路径也不是 PIP 现有 Qt 场景图方案的直接升级。

## 5. 两个模拟发送端

### SharkDataSever

实际图传代码读取视频文件，通过 FFmpeg/libx265、`ultrafast`、`zerolatency` 转成 HEVC，
再用 Node.js UDP 发送。它不是与上述客户端等价的接收解码实现。
[转码](https://github.com/JNU-SHARK/SharkDataSever/blob/fecef639c10b852a96ab3af4ad3fc4dd95f01271/js/udp-video-streamer.js#L142-L171)。

值得注意的是，stdout chunk 先追加到 `pendingFrameBuf`，由 `setInterval` 按视频帧率
取走当前全部缓存，给这一批字节分配一个 frame ID；没有按 HEVC 访问单元边界拆帧。
所以这里的一个 UDP “frame”可能不是一个编码图像，也可能包含多个图像。
积压时还会整批丢字节，可能截断 NAL 或破坏参考链。
[定时批次与发送](https://github.com/JNU-SHARK/SharkDataSever/blob/fecef639c10b852a96ab3af4ad3fc4dd95f01271/js/udp-video-streamer.js#L174-L220)、
[分片头](https://github.com/JNU-SHARK/SharkDataSever/blob/fecef639c10b852a96ab3af4ad3fc4dd95f01271/js/udp-video-streamer.js#L252-L288)。

**意义：**该服务可辅助连通性测试，不宜未经修正直接作为精确 RX FPS、丢帧率或 60 FPS low 的基准源。

### RMMock

主体是 GoCV 读取视频/相机，经 cgo 调 libavcodec 的 `libx265` 软件编码，再分片 UDP 发送。
编码器设置 GOP=30、无 B 帧；调用处使用 `veryfast`、`zerolatency`。
这是编码侧的选择，不能作为硬件解码的证据。
[编码器](https://github.com/stydxm/RMMock/blob/03ea98b14632bf9352bec602ce89d1a708bab2f8/pkg/stream/ffmpeg_encoder.go#L43-L89)、
[发送循环](https://github.com/stydxm/RMMock/blob/03ea98b14632bf9352bec602ce89d1a708bab2f8/pkg/stream/encoded_stream.go#L23-L84)。

附带的 Python 接收示例将 UDP 分片拼好后，一边写文件一边写 ffplay stdin，
配置 `nobuffer`、`low_delay`、小探测窗口，没有显式 `-hwaccel`。
收包、磁盘写入和管道写入在同一循环中，任一阻塞都可能影响下一次 `recvfrom`。
它等下一帧 ID 出现才结算上一帧，且完整性只检查分片序号是否从 0 连续，未核对头中的总长度，
因此尾部分片丢失可能漏检；真正等待 IDR 的逻辑被注释，当前收到参数头即可开放。
[接收脚本](https://github.com/stydxm/RMMock/blob/03ea98b14632bf9352bec602ce89d1a708bab2f8/scripts/receive_video.py#L49-L167)。

**意义：**适合学习最小收流过程，不适合作为比赛客户端的低延迟、抗丢包或真实呈现统计模板。

## 6. 对 PIP-Client 的具体判断

当前 PIP 已有独立接收/解码线程、有界压缩队列、最新画面邮箱、Qt 场景图显示与自动硬解回退。
硬解后仍下载到 CPU，经 swscale 转 RGBA，再上传 Qt 纹理；这是现阶段最值得测量的额外路径。
参见本仓库 [接收器](../../../src/video/video_receiver.cpp)、[解码器](../../../src/video/hevc_decoder.cpp)、
[队列](../../../src/video/video_decode_queue.cpp)、[显示](../../../src/video/video_view.cpp)。

后续建议按以下顺序验证；本轮不实施：

1. **统一源和统计。**固定同一个 HEVC 输入及分辨率、GOP、帧率，记录源 AU 节奏、接收完成、
   解码输出、Qt 呈交四个阶段。管道写入 FPS、paintEvent FPS、frameSwapped FPS 不能互换。
   输入阶段已经不均匀时，提高后端解码吞吐不能凭空恢复源中缺失的时序。
2. **拆开当前解码耗时。**分别测 `send/receive`、硬件帧下载、色彩转换、GUI 取帧、纹理更新和
   带新视频帧的呈交间隔，报告分位数和长尾；不要把含下载、转色的整个耗时都称为纯解码时间。
3. **先评估 YUV/NV12 直显。**保持 HEVC 解码结果为 YUV，在 GPU 做色彩转换，减少 CPU RGBA 转换
   和上传字节数。1080p 的 YUV420 为约 3.11 MB/帧，RGBA 为约 8.29 MB/帧；60 FPS 时分别约
   187 MB/s、498 MB/s，仅是原始数据量，不是实测内存或 PCIe 带宽。需兼容 NV12/P010、
   色彩矩阵、范围、SAR、跨线程所有权及软件渲染回退。
4. **再评估硬件表面共享。**Windows D3D11VA 与 Qt 图形后端、Ubuntu VAAPI 与实际显示后端
   分别验证互操作。必须覆盖 Intel 核显与 NVIDIA 混合显卡，不能仅凭 API 名称承诺零拷贝；
   NPU 不作为视频解码后端。无法共享时保留有界下载路径。

PIP 的 OUT 仅在含新视频纹理的 `QQuickWindow::frameSwapped` 时计数，
比“写管道成功”更靠近呈现阶段，但也不代表显示器扫描完成或精确的端到端延迟。
本次没有做 Windows/Ubuntu 构建、外部程序运行或 GPU 实机测试；新增内容为文档，无运行行为变化。
