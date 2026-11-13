# Qt 6.12.0 实际构建与延迟对照

日期：2026-11-13。本次使用官方 Qt 6.12.0 SDK 实际编译、运行和部署客户端，
并与 Qt 6.9.0 交替测量。此前的[源码研究](qt_video_render_wait_research.md)
只证明实现存在差异，不能代替本记录的运行结果。

## 版本与工具链

| 项目 | Windows | Ubuntu |
| --- | --- | --- |
| 系统 | Windows 11，x86_64 | WSL Ubuntu 22.04.5 LTS，x86_64 |
| Qt / QtTest | 官方 6.12.0 动态 SDK | 官方 6.12.0 动态 SDK |
| SDK 自身构建编译器 | GCC 15.1.0 | GCC 11.5.0 |
| 项目编译器 | MinGW GCC 13.1.0 | Ubuntu GCC 11.4.0 |
| CMake | 4.2.0 | 缓存中的官方 3.31.8 |
| Qt MQTT | v6.12.0 源码，本地构建 | 尚未构建 |
| 验证范围 | 完整产品、CTest、原生 D3D11、部署 | 控制器、设置 QML、日志、崩溃取证组件 |

Windows SDK 仓库元数据列出的工具链依赖为 MinGW 13.1，但库的构建标识为 GCC 15.1。
本轮实际使用已有 MinGW 13.1 编译项目、Qt MQTT 和固定 Protobuf 3.20.3，构建运行成功；
不能将其表述为 SDK 与项目使用完全相同的编译器，也不能外推为其他 ABI 组合已验收。
Ubuntu Qt 6.12 要求 CMake 至少 3.25，原系统 3.22 无法配置，故使用独立缓存工具。

Qt SDK 来自官方在线仓库，逐包对照官方 `.sha1` 校验：

- [Windows 元数据](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_mingw/Updates.xml)。
- [Linux 元数据](https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6120/qt6_6120/Updates.xml)。
- [Qt MQTT v6.12.0 源码](https://github.com/qt/qtmqtt/tree/v6.12.0)。

版本由 `qmake -query QT_VERSION`、CMake 模块版本、QtTest 的实际运行头确认；
Windows QtCore 文件版本为 6.12.0.0。Ubuntu 四程序的 `ldd` 均只解析新 SDK 的 Qt 库。
Windows 为新 kit 单独指定插件、QML 导入目录和子进程 PATH；不修改系统 Qt 或全局环境。
FFmpeg 7 使用独立运行库目录，和 6.9 对照所用三项核心 DLL 的 SHA256 相同。
Qt MQTT 的可选 WebSockets 组件缺失，本次验证现有 TCP MQTT 路径。

## 功能与部署

| 范围 | 结果 |
| --- | --- |
| Windows 完整 Debug 构建 | 168 步成功，包含产品 QML 编译及 FFmpeg 部署 |
| Windows CTest | 15/15 通过，134.73 秒 |
| Windows 原生界面 | 自动硬解 / CPU × 同步开启 / 关闭，四组均退出 0 |
| Windows 静态检查 | Qt 6.12 `all_qmllint` 修正后无警告 |
| Windows 独立安装启动 | 隔离开发环境 PATH，原生 Direct3D 11 启动并正常退出 |
| Ubuntu 应用控制器 | 26 passed、0 failed、0 skipped |
| Ubuntu 真实设置 QML | 9 passed、0 failed、0 skipped，offscreen/software |
| Ubuntu 日志 | 10 passed、0 failed、0 skipped |
| Ubuntu 崩溃取证 | 13 passed、0 failed、0 skipped |

Ubuntu 合计 58 条通过，包含初始化和清理条目，不代表 58 个独立产品功能。
原生界面旧输出没有捕获 QtTest 正文，四组仅按退出状态记通过，不编造用例数量。
新版 QML 类型检查发现 `ScrollView.contentItem` 的公开类型为 `Item`，
不能直接静态解析 `contentY`；现先确认实际对象为 `Flickable` 再重置滚动位置。
最终 QML 修改后，两版重新构建，原生设置 / 缩放 / 图传菜单专项均为 8 条通过；
6.9 图传窗口专项仍出现此前记录的 `QPainter` 告警，6.12 本次专项未出现。
这不能替代长期观察，也不能断言该告警已在所有环境修复。
6.9 的导入扫描器会递归看到验证缓存内的 6.12 SDK，并打印其 Imagine 组件的语法诊断；
旧版产品构建仍退出 0，6.9 / 6.12 产品 `all_qmllint` 均通过。该诊断来自混放的验证缓存，
不能算成产品 QML 警告已修复，也不应作为新版运行失败的证据。

## 延迟测量方法

两版均为同一产品源码、Debug、MinGW 13.1、相同 FFmpeg 和 Protobuf；
实际图形后端为 Direct3D 11，显示器报告 165 Hz。GPU 为 NVIDIA RTX 4070 Laptop，
自动解码组须确认真实 D3D11VA 硬件帧输出，CPU 组须确认软件后端。

使用现有 `welcome_ui_test::hdVideoReportsActualBackendAndPresentation`，
独立线程按单调时钟发送 1920×1080 / 60 Hz、240 个 AU、无 B 帧的 HEVC 运动 GOP。
每种解码 / 同步组合各交替测试两版三次，每次正常事件循环播放 10 秒，共 24 次。
测试窗口为 1280×720，菜单关闭。清除私有调度实验变量，以产品设置请求同步 1/0。
原客户端和发送服务可以继续运行；本轮测试串行，不能称为独占机器性能基准。

阶段均值 / P95 来自结束时最近一秒的有界快照，界面诊断最多滞后约 250ms，
不是全程 10 秒的统计。重复结果报告逐次值或范围，不平均 P95 作为全程分位数。
`receiveLocal` 从应用读出该帧首包开始，至 Qt `frameSwapped`；不含 socket 读出前等待，
也不包含物理亮屏。`guiWait` 是输出完成至场景图同步，包含平台与渲染调度。
OUT / RX low 使用最近 10 秒的最慢 5% / 1% 间隔平均值的倒数，不能当作一秒 FPS 分位数。

## 延迟结果

24 次测量全部通过；逐次日志均确认实际 Qt 版本、D3D11 图形后端、同步请求及预期解码后端，
自动组均真实使用 D3D11VA，CPU 组均使用软件解码。每次末尾耗时快照有 57–60 个呈交样本。
下表区间是每组合三次快照数值的最小值至最大值，单位 ms；不是置信区间或全程分位数。

| 解码 / 同步 | Qt | 输出至同步均值 | 输出至同步 P95 | 首包至 Qt 呈交均值 | 首包至 Qt 呈交 P95 | OUT fps |
| --- | --- | --- | --- | --- | --- | --- |
| D3D11VA / 开 | 6.9 | 4.000–4.786 | 6.940–7.495 | 10.930–12.005 | 14.587–17.215 | 57–59 |
| D3D11VA / 开 | 6.12 | 4.059–5.274 | 7.287–8.263 | 10.858–13.724 | 14.072–17.992 | 59–60 |
| D3D11VA / 关 | 6.9 | 4.681–5.127 | 7.075–9.146 | 11.772–13.547 | 16.075–18.621 | 58–60 |
| D3D11VA / 关 | 6.12 | 2.083–2.291 | 3.333–4.517 | 8.639–9.848 | 10.823–14.771 | 58–60 |
| CPU / 开 | 6.9 | 4.115–4.773 | 7.090–7.263 | 10.286–11.128 | 13.455–14.435 | 59–60 |
| CPU / 开 | 6.12 | 4.102–5.062 | 7.360–7.697 | 10.466–11.597 | 14.493–14.856 | 59–60 |
| CPU / 关 | 6.9 | 4.097–5.153 | 7.789–8.252 | 10.790–13.209 | 15.456–18.424 | 57–60 |
| CPU / 关 | 6.12 | 2.224–2.513 | 3.402–4.523 | 9.131–9.437 | 12.432–12.775 | 58–60 |

关闭同步时，6.12 在 CPU 和硬解两条路径都呈现更低的输出至同步等待及首包至呈交耗时，
与新版跳过 DXGI vblank 注册的源码方向一致。同步开启时结果重叠且有波动，
没有证据表明直接升级就能消除当前等待，也不能将性能变化全部归因于单条源码分支。

所有调用结束时本地待解码累计丢弃、不完整帧均为 0，最后快照整帧缺口为 0；
不代表物理屏幕显示了所有帧。发送末尾速率为 58–60fps，发送节拍本身有抖动，
全矩阵 OUT 1% low 为 21.87–28.66fps，发送源 1% low 为 23.29–31.69fps。
6.12 关闭同步并没有稳定改善 OUT low：例如 CPU 三次为 25.81、27.73、21.87fps，
对应 6.9 为 28.66、25.96、25.16fps，不能据此承诺满 60fps 或长尾问题已解决。
本轮不更改同步默认开启的产品策略。

## 复现要点

先用项目 CMake 工程分别配置新的构建目录，固定所选 Qt 与同版 Qt MQTT，
显式提供 `PIP_MQTT_ROOT`、`PIP_PROTOBUF_ROOT`、`PIP_FFMPEG_ROOT`；
Windows 用 `PIP_FFMPEG_RUNTIME_DIR` 指定已校验的完整 FFmpeg 动态库闭包。
不要在同一个 CMake 缓存中来回替换 Qt 前缀。

每组使用独立子进程，执行构建目录的
`welcome_ui_test hdVideoReportsActualBackendAndPresentation -o result.txt,txt`。
使用以下测试变量，并清除 `QSG_NO_VSYNC`、`QT_D3D_NO_VBLANK_THREAD`、
`QT_QPA_UPDATE_IDLE_TIME`、`QT_D3D_MAX_FRAME_LATENCY`、`QSG_RENDER_LOOP` 等覆盖：

| 变量 | 值 |
| --- | --- |
| `QT_QPA_PLATFORM` | `windows` |
| `QSG_RHI_BACKEND` | `d3d11` |
| `PIP_UI_TEST_VERTICAL_SYNC` | 每组合为 `1` 或 `0` |
| `PIP_VIDEO_DECODER` | 每组合为 `auto` 或 `cpu` |
| `PIP_VIDEO_TEST_STREAM` | 同一含 AUD 的 HEVC Annex B GOP 文件绝对路径 |
| `PIP_VIDEO_TEST_DURATION_MS` | `10000` |
| `QTEST_FUNCTION_TIMEOUT` | `120000` |

为每版设置各自 SDK 插件、QML 导入与动态库路径，逐次检查 QtTest 的 `Config`，
不能仅靠 PATH 推断实际加载版本。保存 `VIDEO_MEASUREMENT` 完整 JSON、发送节拍、
实际后端、丢弃计数、样本数和同步请求；Windows QtTest 文本可能使用本地代码页，
不能因 UTF-8 解码失败就把退出 0 的调用误报为产品测试失败。

## 工程状态与未验收范围

本轮建立独立 6.12 验证环境；CLion 主配置和原有 Debug 构建仍为 Qt 6.9.0，
未把缓存测试环境无声替换为正式工具链。迁移时须同时固定 Qt、Qt MQTT、编译器、
插件与 QML 运行库，并清理旧版本构建缓存，不能只替换一个前缀。

Ubuntu 完整客户端、Qt MQTT / FFmpeg 视频集成、部署、原生 X11 / Wayland、
Intel / AMD / NVIDIA 的 Linux 硬件路径均未完成本次验收。
Windows 6.12 尚未完成长稳、混合显卡、设备丢失、功耗或物理扫描测量；
短测通过不能继承旧版长稳结果，也不能证明所有设备稳定或实际亮屏更快。

本机原始证据位于忽略的 `.cache/qt612_validation/`：`windows_logs/`、
`windows_measurements.json`、部署记录与 `linux/report.md`；不提交 SDK、日志或机器路径。
