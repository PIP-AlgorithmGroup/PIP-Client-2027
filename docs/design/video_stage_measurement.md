# 视频分阶段测量与设备选择验收

日期：2026-11-13。目标是建立可归因的本机观测，并让用户选择实际可探测的 HEVC 解码设备。
本记录不宣称已解决用户实流 low 帧问题，不新增编码发送链路。

## 测量契约

链路为完整 AU 重组 → 有界解码队列 → codec 调用 → 可选硬件下载 → RGBA 转色
→ 单槽邮箱 / GUI / 渲染同步 → CPU 纹理准备 → Qt 呈交。
各阶段最近 1 秒最多保留 512 个样本，每 250ms 发布均值、P95、P99、最大值及样本数。
阶段定义及操作见[图传接收](../video_receiver.md)。

组帧单调微秒通过 FFmpeg packet PTS 传到实际输出帧，不将延迟输出与最新输入的组帧时间混用。
codec 成本包括驱动同步调用；下载也可能等待 GPU。纹理准备只测 CPU，未使用 GPU timestamp query。
`frameSwapped` 不是显示器扫描完成，组帧到呈交不包含采集、编码和传输，不能称为端到端延迟。
不同阶段的样本集合和快照时刻不同，不应将各阶段分位数相加。

## 本机设备探测

Windows 11，Qt 6.9，MinGW 13.1，FFmpeg 7；CPU 为 Intel Core i7-14700HX，
GPU 为 NVIDIA GeForce RTX 4070 Laptop GPU，驱动 32.0.15.9579。

| 设备 / 后端 | 本机结果 |
| --- | --- |
| Intel Core i7-14700HX / CPU | 软件解码、阶段统计和实际输出通过 |
| RTX 4070 Laptop GPU / D3D11VA | DXGI 提供两个不同 LUID 的适配器条目，界面用 GPU 编号区分；逐项选择均输出真实硬件帧 |
| RTX 4070 Laptop GPU / CUDA | 驱动可枚举，当前 FFmpeg 原生 HEVC 缺少该后端，禁选并报告原因 |
| Intel 核显 | 本机 DXGI 未枚举出，未实测；不能以 Intel CPU 的存在推断核显已启用 |
| Ubuntu VAAPI、AMD、Intel Core i / Ultra 核显 | 有探测/选择路径，未完成构建及实机验收 |

上下文初始化通过只标记可尝试，不标记 `hardwareActive`；实际码流可能因 profile、格式或驱动失败。
手动选择失败回退 CPU，自动模式才继续尝试其他设备。设备身份失效保留配置与原因，需重新选择。
Windows LUID 不保证跨重启/驱动变更持续有效；同一物理 GPU 的多个后端不是不同显卡。
NPU 不承担该 HEVC 解码链路，不作为可选解码设备。

## 原生 Windows 顺序短测

两次测量顺序运行，不并行构建/测试；Qt 使用原生 Windows 平台和 Direct3D 11。
同机独立线程按 60 Hz 发送自产 1920×1080 HEVC IDR，正常 Qt 事件循环约 2.5 秒后取快照。
此简单重复 IDR 只验证测量链路，不代表真实摄像头 GOP、官方设备、复杂运动或长期负载。
解码阶段约 60/61 个样本，呈交约 58–60 个；P99 近似最大值，需实流持续采样再判断。

| 最近 1 秒阶段，单位 ms（均值 / P95 / P99） | 自动 D3D11VA | 强制 CPU |
| --- | --- | --- |
| codec 调用 | 0.46 / 0.64 / 0.70 | 2.74 / 3.22 / 3.35 |
| 硬件帧下载 | 2.66 / 3.95 / 4.62 | 未执行 |
| RGBA 转色 | 9.74 / 11.46 / 11.82 | 1.64 / 1.95 / 2.17 |
| 解码排队 | 0.58 / 0.70 / 13.01 | 0.02 / 0.03 / 0.06 |
| 输出至渲染同步 | 5.39 / 9.82 / 14.65 | 4.84 / 7.86 / 14.15 |
| CPU 纹理准备 | 0.02 / 0.03 / 0.03 | 0.02 / 0.02 / 0.03 |
| 准备至 Qt 呈交 | 2.64 / 3.45 / 4.00 | 1.96 / 2.28 / 2.58 |
| 同帧组帧至 Qt 呈交 | 21.81 / 29.21 / 33.50 | 11.50 / 16.22 / 21.03 |

| 帧率及间隔统计 | 自动 D3D11VA | 强制 CPU |
| --- | --- | --- |
| RX / DEC / OUT | 59 / 60 / 58 | 59 / 60 / 58 |
| 测试源 1% low | 31.63 | 25.48 |
| RX 1% low | 31.41 | 25.43 |
| OUT 1% low | 23.31 | 23.59 |
| 本地待解码 AU 累计丢弃 | 0 | 0 |

本次硬解 codec 调用较短，下载与转色占用了更多时间；当前复制显示路径没有体现整体耗时优势。
这只说明本机该样本的观测结果，不能推广为所有 GPU/码流均慢于 CPU，也不能将所有 OUT 抖动归因于转色。
发送源自身已有约 32–39ms 的最慢间隔，客户端呈交 low 还受 GUI、场景图和系统调度影响。
下一步应在用户真实码流中收集同样阶段，再评估硬件像素格式转色、GPU 纹理共享及呈交调度，
避免通过堆积播放缓冲或重新开启曾死锁的 HEVC slice/WPP 并行掩盖问题。

## 复现入口与验证边界

使用已配置的构建环境，保证 Qt、MQTT 和 FFmpeg 运行库可解析。
Windows 原生短测设置 `QT_QPA_PLATFORM=windows`、`QSG_RHI_BACKEND=d3d11`，
分别令 `PIP_VIDEO_DECODER=auto` 与 `cpu`，顺序执行：

```powershell
./cmake-build-debug-qt-mingw/welcome_ui_test.exe hdVideoReportsActualBackendAndPresentation -o measurement.txt,txt
```

输出的 `VIDEO_MEASUREMENT` JSON 包含 receiver、presentation、sender 和实际 graphics 后端。
原始本机日志保留在忽略的 `.cache/video_measurement_auto_windows.txt`、
`.cache/video_measurement_cpu_windows.txt`；不提交机器专用日志。
发送统计在线程 join 后取值，与接收/呈现快照略有时差；不能按单个快照计数差推断丢帧。

自动测试覆盖确定性窗口/分位数/容量、CPU 无下载样本、设备初始化与真实硬件帧区别、
失效选择回退、配置注入拒绝、选择持久化、运行中重配及界面草稿应用/取消。
设备探测独立于 GUI 和收包线程；析构 join，驱动内部挂起无法强制取消，尚无最坏停止时限保证。
没有完成 Ubuntu 构建、Intel/AMD 实机、真实 GOP 30 分钟长稳、GPU timestamp、功耗与内存验收。

Windows 完整 CTest 10/10 通过；帧时间戳关联补齐后相关 5 项再次通过。
最新设备下拉键盘选择、弹层原因展示、保存/取消及共享兵种下拉原生窗口回归通过。
完整构建、clang-format、qmlformat 一致性、qmllint 和差异空白检查通过。
本地界面截图保留于忽略的 `.cache/video_stage_captures/`，设备控制使用原有滚动表单和固定底部动作。
