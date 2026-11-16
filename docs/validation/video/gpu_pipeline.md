# GPU 视频通路验证

## 实现与验收边界

日期：2026-11-16。Qt 固定 6.12.0、FFmpeg 7.1，保留 Qt Quick/QML 调度和现有 UDP/AU 恢复策略。
本次目标是移除正常硬解显示中的 CPU 像素下载、转色和 RGBA 重新上传，不替换视频呈交线程。
试验预算为本机 1080p60：共享成功时两个 CPU 转色/下载阶段均无样本，稳定输出接近输入，
首包至 Qt 呈交均值争取小于一帧（16.67ms）、P95 小于两帧；这些是本机实验目标，不是硬件矩阵承诺。
同时记录 CPU、进程内存和持续接收结果；本次没有 GPU 时间戳、扫描完成或功耗测量。

帧契约从必有 RGBA 图像改为不可变、引用计数的 `AVFrame`，保留原始格式、矩阵、范围、SAR 和
首包/AU 编号元数据。解码器重置、停止和销毁后，消费者持有的帧仍有效；解码池不能覆盖活跃引用。
GUI/邮箱不执行像素转换，旧 surface 的最后引用移到邮箱锁外释放，避免驱动回收阻塞发布/收包。
正常解码计时止于像素引用就绪，显示回退成本单独记录，避免把等待藏起来。
`VideoFrame::toImage()` 是显式快照/测试入口，不是逐帧正常通路。

| 显示路径 | 行为与边界 |
| --- | --- |
| `d3d11-shared-yuv` | FFmpeg decoder texture array slice → 解码设备共享 NV12/P010 纹理 → Qt 设备本地纹理；两次 GPU 内复制，Y/UV SRV 加 shader，无 CPU 像素下载 |
| `vaapi-egl-yuv` | FFmpeg 直接导出 DRM PRIME，导入 EGL R8/GR88 独立层，GPU 复制至 Qt 平面后 shader；仅实现当前 NV12 布局，须实机验收 |
| `yuv-upload` | 软件 YUV 帧或硬件下载帧，复用 R8/RG8/R16/RG16 纹理并 GPU 转色；此路径仍有 CPU 上传 |
| `rgba-upload` | 软件场景图、显式旧路径或不支持的像素格式；CPU 转 RGBA，再用 Qt 纹理显示 |

D3D11 生产者/消费者各自使用 keyed mutex，单次 `AcquireSync` 超时 5ms；解码 immediate context
复制提交使用 FFmpeg 设备锁。源 slice 的复制先于该池引用归还后的后续解码提交，Qt 本地纹理的读写
在 Qt 自己的 context 中有序执行，不把未完成读写的共享纹理直接交给下一帧覆盖。
规格不变时复用三张 GPU 视频纹理和两张平面 SRV；Intel/NVIDIA/AMD 不按厂商名硬编码。
跨适配器共享失败时保留原因并下载 YUV；同一解码池失败后不逐帧重试，换池/重建场景图重新探测。

EGL 路径校验独立层格式、对象索引和 modifier 扩展；GPU 复制后以 GL fence 保留源映射、fd 与
VA surface，最多六个在途复制。正常轮询 fence 不阻塞，场景图销毁时完成剩余复制再释放资源。
FFmpeg `AV_HWFRAME_MAP_READ` 内部的 VA 同步、设备锁、驱动调用和销毁 `glFinish` 没有统一的
可移植强制取消保证；5ms keyed-mutex 不能推广为所有驱动调用的等待上限。
GLX、Vulkan、CUDA、P010 VA 导入和未知布局明确回退；不声称所有 Linux API 都有硬件直通。

运行诊断与日志中的 `renderPath` / `renderFallback` 是实际显示结果；`hardwareActive` 仅表示硬解输出。
`gpuBridge`、`renderDownload`、`renderConvert` 是 `texture` 的 CPU 子阶段，不重复相加。
它们不代表 GPU 执行时间。`receiveLocal` 直接计量同一帧首包读出至 Qt `frameSwapped`，
仍不包含相机采集、编码、到内核前传输或显示器扫描。

## 回归与环境

Windows：Windows 11 x64、应用使用 MinGW 13.1、Qt 6.12.0 SDK 使用 GCC 15.1 构建、FFmpeg 7.1、
RTX 4070 Laptop（595.79 驱动，8188MiB）、165Hz 屏幕。
客户端为 Debug 构建；Qt/FFmpeg 是所选 SDK 的共享库，不能当作 Release 产品性能。
Ubuntu：WSL Ubuntu 22.04、GCC 11.4、同版 Qt 与软件 FFmpeg；没有将 WSL 当作原生 Linux GPU 验收。
两平台均从同版源码补齐 Qt ShaderTools，只有 qsb 在构建时使用；程序不链接 ShaderTools 运行库。
QRhi 依赖 GuiPrivate，升级 Qt 需要重新构建和图形回归。

自动化检查覆盖：

- 解码输出时间关联、解码器重置/销毁后保留像素、无效后端与设备回退、真实 UDP 和会话释放。
- 四种 YUV 格式 × 三种矩阵 × 两种范围，共 24 组；每组检查连续像素更新、纹理复用、规格变化、
  奇数尺寸、负 stride、四象限颜色和清屏。CPU 参考容差每通道 6/255，避免把 GPU 浮点和
  swscale 整数舍入差异当作错误；该测试不验证 HDR 或色彩管理。
- 自产 NV12/P010 码流的真实硬解/共享，重复 12 帧、与 CPU 下载参考比较，容差每通道 8/255；
  解码器重置与场景图销毁/重建后仍可显示保留帧。
- 原生 D3D11 强制要求 `d3d11-shared-yuv`，不能静默回退后算共享通过。
- Windows OpenGL 强制要求 `yuv-upload`，验证跨 API 失败原因和 YUV 回退。
- Ubuntu WSLg Wayland/OpenGL shader 对照使用 Mesa 23.2.1、OpenGL 4.2，渲染器为
  `D3D12 (NVIDIA GeForce RTX 4070 Laptop GPU)`；这是 WSLg 的 GPU shader 验证，不能证明 VAAPI 导入。

Windows 完整构建、19/19 组 CTest、`all_qmllint` 和变更 C++ 的 clang-format 检查通过。
原生 D3D11 与 OpenGL 专项分别 28 项通过（均包含初始化/清理），覆盖真实 NV12/P010；
D3D11 的实际路径为 `d3d11-shared-yuv`，OpenGL 为带明确原因的 `yuv-upload`。
Ubuntu 完整构建和 18/18 组 CTest 通过（159.02 秒）；其中纹理专项 26 项通过、
NV12/P010 硬件子项各跳过一次，原因是该验证 FFmpeg 未包含 VAAPI/CUDA 后端。
WSLg Wayland/OpenGL 平面颜色专项 26 项通过（24 组颜色加初始化/清理），
没有将软件场景图或硬件跳过计作原生共享通过。

两平台 `cmake --install` 成功，包含插件、QML、FFmpeg 和许可文件；qsb/ShaderTools 不进入运行闭包。
Windows 安装目录在只保留系统 PATH 的环境下完成 offscreen/software 五秒启动检查；
Ubuntu 清除开发库和 QML 路径后，主程序 `ldd` 无缺库，Qt/FFmpeg/ICU 均解析到安装目录，
同样完成 offscreen/software 五秒启动检查。
该环境此前缺失的 X11 系统库不在本次闭包验收范围；原生 Linux 桌面发布仍需另行验收。
启动短测的主动终止不算正常退出或稳定性测试。

## 同机性能对照

使用 1080p60、含 AUD 的 240 AU GOP 回环发送，threaded 渲染循环，关闭垂直同步。
各组独立进程、串行运行；10 秒播放组按 auto → rgba → rgba → auto 交替，另跑 YUV 回退和 CPU。
以下 FPS、延迟和解码耗时均为结束时最近一秒窗口，**不是整场平均**。
P99 与最大值在这些窗口内相同；首包终点为 Qt 呈交，不能解读为显示器真实亮屏。

| 模式/次序 | 输出 FPS | 首包→Qt 均值 ms | P95 ms | P99/最大 ms | 解码总均值 ms | 进程 CPU 秒 | 采样 RSS 峰 MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| auto A | 57 | 18.754 | 20.493 | 37.039 | 0.443 | 3.047 | 374.7 |
| rgba A | 60 | 11.245 | 15.425 | 26.429 | 5.763 | 12.234 | 455.8 |
| rgba B | 60 | 10.790 | 16.627 | 19.965 | 5.861 | 12.875 | 453.0 |
| auto B | 59 | 3.968 | 9.650 | 20.653 | 0.276 | 4.172 | 419.1 |
| yuv（硬件下载） | 52 | 8.203 | 16.621 | 36.148 | 0.873 | 5.797 | 435.3 |
| cpu（软件解码/GPU 转色） | 59 | 7.320 | 14.778 | 21.212 | 3.400 | 7.125 | 432.9 |
| auto 持续 120 秒末窗 | 51 | 18.534 | 29.678 | 48.562 | 0.599 | 21.969 | 422.1 |

CPU 秒是进程 user+system 累计，包含启动/清理的大部分时间；10 秒播放实际进程约 12 秒。
RSS 每 0.5 秒采样，峰值不是 GPU 显存，也不是操作系统记录的严格进程峰值。
两次 auto 的 CPU 秒均值约 3.61，对照 rgba 的 12.55，下降约 71%；这是本机 Debug 测量。
各组均无解码队列溢出丢帧，不能据此推断 Qt 没有合并/跳过显示帧。

原生共享运行的解码侧和显示侧 CPU 下载/转色均无样本，GPU 内复制的 CPU 观测均值约
0.28–0.67ms。消除了 CPU 像素往返，但**端到端延迟没有稳定改善**：auto A 的 renderWait
均值为 14.985ms，auto B 为 1.334ms，rgba 对照约 1.7–2.2ms；不能挑选 auto B 的 4ms 当承诺。

120 秒持续接收中，30/60 秒窗口输出为 60/59fps，首包→Qt 均值 4.257/3.995ms，P95
7.313/6.279ms；90 秒为 56fps、均值 17.853ms，120 秒末窗为上表 51fps。
后两窗 renderWait 约 14.2/14.4ms，输入仍约 59fps。
RSS 在 10–30 秒约 407.52–409.50MiB，90–120 秒约 410.27–411.12MiB；
两分钟运行不构成长时间无泄漏证明。
因此首包→Qt 均值小于 16.67ms、输出接近 60fps 的试验预算尚未稳定达成。
下一步应围绕 Qt 唤醒/同步等待继续测量，再决定是否采用独立视频呈交线程/交换链。
以上对照早于最后的邮箱锁外释放修正；该修正后的回归与持续运行另行记录，不混成同一次测量。

### 最终代码持续运行复核

邮箱锁外释放修正后，重新完成上述双平台完整回归、D3D11/OpenGL 28 项专项及 WSLg 26 项专项。
另以相同硬件、GOP 和图形设置连续播放 60 秒，实际路径一直为 `d3d11-shared-yuv`，测试通过：

| 窗口 | 输出 FPS | 首包→Qt 均值 ms | P95 ms | P99/最大 ms | renderWait 均值 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 第 30 秒快照 | 59 | 6.373 | 14.993 | 28.378 | 1.245 |
| 第 60 秒末窗 | 59 | 6.249 | 10.496 | 25.126 | 1.069 |

末窗解码总均值 0.471ms、互操作 CPU 均值 0.524ms，解码侧和显示侧下载/转色均无样本，
解码队列丢弃累计为零。进程 CPU 18.0 秒、采样 RSS 峰 419.9MiB，含启动和清理，
运行开头还并行执行过五秒部署启动短测；这组只作最终功能复核，不与之前组重算性能差值。
单次短测达到预算不能否定此前 120 秒后段的等待升高，也不能把改善归因于锁外释放这一项。

## 复现

普通完整回归使用既有构建目录；显式软件场景图用于无图形硬件的基础验证：

```sh
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
cmake --build build --target all_qmllint
```

Windows 原生专项（Qt/编译器运行库已在 PATH）：

```powershell
$env:QT_QPA_PLATFORM='windows'
$env:QSG_RENDER_LOOP='threaded'
$env:PIP_VIDEO_TEST_REQUIRED_PATH='d3d11-shared-yuv'
Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
Remove-Item Env:QSG_RHI_BACKEND -ErrorAction SilentlyContinue
& build/windows/qt612-debug/video_texture_test.exe -o texture_d3d11.txt,txt
```

性能运行使用 `welcome_ui_test hdVideoReportsActualBackendAndPresentation`：
`PIP_VIDEO_TEST_STREAM` 指向含 AUD 的 Annex B GOP，`PIP_VIDEO_TEST_DURATION_MS` 设置播放毫秒，
`PIP_UI_TEST_VERTICAL_SYNC=0` 通过产品偏好关闭同步；QtTest 每 30 秒输出有界 `VIDEO_STABILITY`，
最终输出 `VIDEO_MEASUREMENT` JSON。按 `auto → rgba → rgba → auto` 顺序交替，另测 `yuv` 和
`PIP_VIDEO_DECODER=cpu`；改变模式/解码设备时启动新进程。不要同时运行其他主动构建或测试来比较延迟。

Ubuntu 专项需实际图形环境；WSLg 已用 `QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=rhi
QSG_RHI_BACKEND=opengl` 运行平面 shader 颜色测试。仅设置 `QSG_RHI_BACKEND=opengl` 并不保证
真正使用 GPU，offscreen 可能仍选择 software，必须检查 QSG 日志及实际路径。

## 未验收项

Intel 核显（含 U 系列）、AMD、混合显卡跨适配器共享、Ubuntu 原生 X11/Wayland VAAPI 互操作、
真实驱动重置/设备丢失、长时间功耗和 GPU/共享内存占用尚未实机验证。
驱动异常下的自动恢复不能由普通窗口重建测试推断。
本次没有独立交换链，GUI 阻塞仍可能暂停视频更新；是否需要进一步隔离，须在新通路上继续测量。
