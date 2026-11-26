# PIP-Client

RoboMaster 2027 PIP 战队自定义终端。

当前预发布：**v0.4.0-rc.1**，前往
[GitHub Releases 下载 Windows / Ubuntu 安装包](https://github.com/PIP-AlgorithmGroup/PIP-Client-2027/releases/tag/v0.4.0-rc.1)。
变更与验证边界见[发行说明](docs/releases/v0.4.0-rc.1.md)，自行打包见
[打包流程](docs/development/releases.md)。

开发分支新增手动检查更新：欢迎页“关于”或比赛菜单“软件更新”中检查、下载并校验安装包，
返回欢迎页后确认安装，退出后由系统安装器升级并重新启动。便携版保留手动安装；
该功能尚未包含在上方已发布的 v0.4.0-rc.1 中，流程与边界见[软件更新](docs/development/updates.md)。

当前协议基线为 RoboMaster 2027 通信协议 V1.0.0（2026-10-29）。项目已维护自定义终端的
Protobuf 定义、官方 MQTT 双向链路、比赛会话、欢迎页、原生键鼠输入和本机回归测试；RMUL HUD 已实现，比赛遥测尚未接入。
RMUL 已接入 UDP HEVC 图传；自定义控制器串口已接入 `0x0311` → MQTT `CustomControl` 转发。详见
[2027 自定义终端协议](docs/protocol/protocol_2027.md)。

应用启动显示欢迎页，可选择 RMUL 高校联盟赛或 RMUC 超级对抗赛。
RMUL 进入后图传默认停止，在设置页手动开启后，画面等比居中铺满、留黑边；
无画面时居中显示实际接收状态和 Esc 菜单入口；HUD 独立叠加在视频上。
RMUL HUD 包含双方 1/3/7 号血量卡、计时与控制度、目标与热量、Silver 小地图、经济和底部状态。
未接入的血量、热量、功率、超电等字段按未知状态显示，不以零值或演示数据代替；小地图仅显示静态底图。
边缘组件按实际窗口吸附，在 16:9、16:10、4:3 和超宽窗口中保持边距，中心准星与视频维持各自比例。
Esc 打开设置与自检工作区，包含准备概览、MQTT、图传、自定义控制器、键鼠、显示和诊断。
各项目可自由切换，同场保留草稿；完整 ANSI 104 键盘支持窄窗横向查看，悬停时滚轮仍可上下滚页。
键鼠“启用输出”每场默认关闭，需手动开启并完成实际操作检查后确认可用。
本场曾有实际自检通过后，返回欢迎页需二次确认；取消保留准备状态，确认后重新进入需重连并自检。
RMUL 使用明亮中性灰和橙色强调；监听端点等详细信息放入 ESC 菜单。RMUC 保留占位页。
RMUL 右上角直接叠加精简诊断，左侧收帧/解码/呈交 FPS，右侧键鼠状态，横向排列并对齐上边缘；
详细视频统计在 Esc 诊断页查看；
统计与精确 UDP 包损的限制见[图传接收](docs/modules/video_receiver.md)。

0.2 系列在 Windows D3D11 和 Windows/Ubuntu OpenGL 下使用 Qt RenderControl/QRhi 独立呈交线程，
直接消费解码后的最新帧邮箱；GUI 忙时视频继续更新，HUD 保留上次同步状态。
键鼠原生采集与 MQTT 发布可独立于 GUI 执行，界面只读取诊断快照。
正常 GPU 路径使用 YUV shader 转色，并按设备能力共享硬件纹理；共享不可用时下载 YUV 后上传，
软件场景图使用 RGBA 回退。软件/offscreen/minimal 和未覆盖的图形 API 保留 Qt 原呈交循环。
该方案不要求独显，也不保证所有硬解后端都能零下载。实现与验证见
[GPU 视频通路](docs/validation/video/gpu_pipeline.md)和
[独立呈交](docs/validation/video/independent_presentation.md)。

MQTT、串口收发/转发、UDP 接收与解码在后台执行；系统枚举、网络校验及会话保存也已异步化。
GUI 仍负责导航、状态协调、部分配置写盘和启停等待，具体边界见
[GUI 与后端检查](docs/validation/video/gui_backend_separation.md)。

启动和模式选择不连接 MQTT；RMUL 进入后也不自动接收视频。RMUL/RMUC 的 Esc 当前会话使用一套连接配置，
网卡、本机 IPv4、MQTT 地址和端口均可配置，机器人身份先选红蓝阵营，再选兵种，自动映射 ID 后保存并连接；
提供官方默认值标注与草稿恢复入口。
官方订阅由 C++ 协议表管理，尚未接入 HUD 的业务字段保持未知状态。
同一席位 RJ45/网卡承载 MQTT 和 UDP，链路可独立故障与恢复。操作与本地验收见[本地接入](docs/development/local_integration.md)。
图传配置、依赖与验证范围见 [图传接收](docs/modules/video_receiver.md)。
通用入口、配置边界和后续接入方式见 [数据链路](docs/modules/data_pipeline.md)。
控制器默认 921600/8N1/无流控，设置页可改串口参数；固定官方帧封装和 `0x0311`，
有效负载最多 60 字节，MQTT 离线不缓存旧控制。设置与自检期间只本地解析，不转发控制。
使用与跨平台边界见[控制器串口](docs/modules/serial_controller.md)和[键鼠输入](docs/modules/keyboard_mouse_input.md)。
每次启动会保存独立本地日志，欢迎页和 Esc 的诊断页可打开日志目录；
日志、崩溃证据、跨平台应用图标与安装打包方式见[本地诊断与桌面部署](docs/development/diagnostics_and_deployment.md)。

欢迎页、RMUL/RMUC 模式选择与背景素材方案见
[欢迎页设计](docs/design/welcome/design.md)，实际行为、配置与验证见
[欢迎页实现](docs/validation/welcome.md)。设置、诊断、帮助、关于均可使用。
Tab / Shift+Tab 移动焦点，左右键切换赛事，Space 选择，Enter 激活；F11 切换全屏，
欢迎页 Esc 关闭面板或退出全屏，RMUL/RMUC 中 Esc 打开/关闭客户端菜单。动效默认关闭，可在设置中开启。
设置提供 UI 小/中/大三档（90% / 100% / 110%，默认中）；调整窗口只重新布局，
不改变字号与按钮大小，切换赛事保留窗口尺寸/最大化状态。中文使用随包 Noto Sans SC 无衬线字体。
右侧工具使用图标，悬停或键盘聚焦可查看名称。

Windows 与 Ubuntu 为同等目标平台。后续视频、3D 与推理模块必须遵守
[平台与硬件兼容约束](docs/platform_compatibility.md)，覆盖 Intel 核显、可用 NPU 和 NVIDIA 独显。
本版已完成 Windows/Ubuntu 构建和回归，以及 Windows D3D11/OpenGL、WSLg OpenGL 验证。
Intel/AMD 核显、混合显卡、原生 Ubuntu VAAPI、驱动恢复和多小时长稳仍待实机验收；
详细范围以[本版发行说明](docs/releases/v0.4.0-rc.1.md)与发行附件的验证记录为准。

图标、字体、背景、官方手册与素材、设计记录统一从[资源总览](docs/resource_index.md)按任务查阅。

## 当前基础技术栈

- Windows / Ubuntu
- C++20
- CMake 3.25+
- Qt 6.12.0（固定版本）
- Qt Quick / QML / Quick Controls 2 / Qt SVG
- Qt ShaderTools 6.12.0（构建时烘焙 shader）与同一 Qt kit 的 GuiPrivate/QRhi 头文件
- Qt MQTT（与 Qt kit 同版；GPL-3.0-only 或商业许可）
- Qt SerialPort 6.12.0（支持 LGPL-3.0 / GPL / 商业许可）
- Protobuf 3.20.3（BSD-3-Clause；`protoc` 和 C++ 运行库使用相同版本）
- FFmpeg 7.x shared SDK（libavcodec 61 / libavutil 59 / libswscale 8；HEVC 软件解码基础路径）

## 环境准备

安装 Qt 6.12.0、Qt Quick Controls 2、Qt SVG、Qt ShaderTools、Qt MQTT 和 Qt SerialPort 6.12.0，
并选择与编译器匹配的 kit。独立呈交使用 GuiPrivate/QRhi，必须保留该 kit 的私有开发头文件；
升级或替换 Qt 构建后需要重新编译并验证，不能混用其他 Qt 版本的头文件、DLL 或插件。
Windows 使用匹配的 MinGW 或 MSVC，Ubuntu 使用匹配的 GCC 或 Clang；
各平台分别构建依赖和产物，并在开发、CI 与发布中统一对应工具链。
项目和本机 CLion 主配置已迁移到 Qt / Qt MQTT 6.12.0，CMake 拒绝其他 Qt 版本。
Windows 使用新的构建目录，Qt 插件、QML 导入和运行库路径同步切换，详见
[6.12 实测与迁移记录](docs/validation/qt612.md)。若 MQTT 单独安装，配置
`PIP_MQTT_ROOT` 为其安装前缀。
若 SerialPort 独立安装，设置 `PIP_SERIALPORT_ROOT` 为同版模块的安装前缀。
若 ShaderTools 独立安装，将其前缀加入 `CMAKE_PREFIX_PATH`（与 Qt kit 前缀用分号分隔），
或设置 `Qt6ShaderTools_DIR` 指向其 `lib/cmake/Qt6ShaderTools` 目录；工具和模块须同为 6.12.0。
Ubuntu 另需 EGL 开发头文件（例如 `libegl1-mesa-dev`），用于可选 VAAPI/EGL 纹理互操作的能力探测。

使用同一工具链安装 Protobuf 3.20.3。CMake 默认从
`third_party/installed/<Windows或Linux>/protobuf-3.20.3` 查找；
使用其他安装位置时，设置 `PIP_PROTOBUF_ROOT` 或 `Protobuf_DIR`。生成的 C++ 文件保存在构建目录。

安装 FFmpeg 7.x 共享开发 SDK，设置 `PIP_FFMPEG_ROOT` 指向其 `include/lib/bin` 前缀。
Windows 导入库需要匹配 MinGW/MSVC；Ubuntu 也需明确安装版本 7，不能直接使用发行版的 6/8。
CMake 不在构建时下载依赖。Windows 构建检查并复制 FFmpeg 的直接及传递 DLL 依赖；
头文件与导入库单独供应时，可设置 `PIP_FFMPEG_RUNTIME_DIR` 指定完整运行库目录。
发布时同时携带许可及源码信息。
详细依赖来源及当前软件解码限制见 [图传接收](docs/modules/video_receiver.md)。

日常构建目录使用 `build/windows/qt612-debug` 或 `build/linux/qt612-debug`，
依赖源码、安装 SDK 和历史构建按[目录约定](docs/directory_layout.md)维护，
全部文档入口见[文档索引](docs/README.md)。

构建后运行 `ctest --test-dir <构建目录> --output-on-failure` 检查消息定义、二进制编码和
本地 MQTT → Protobuf → QML 通知链路，以及本地偏好与欢迎页交互。GUI 测试使用所选
Qt kit 的 offscreen 插件，不能只部署 qwindows。Windows 运行时，Qt 与独立安装的 Qt MQTT 的
`bin` 目录均须可被 DLL 加载器找到；安装应用后需一并部署依赖。

如果 CMake 找不到 Qt，设置 `CMAKE_PREFIX_PATH` 指向本机对应的 Qt kit。
Windows 的 Qt、MinGW 和依赖必须使用兼容 ABI；Ubuntu 同样需要匹配 Qt、编译器和系统依赖。
机器专用路径只放在本地 IDE 或 CMake 配置中。

## 使用 CLion

1. 启动 CLion，选择 `Open`，打开项目根目录。
2. CLion 会读取根目录的 `CMakeLists.txt`。
3. 在 Toolchains 中选择与 Qt kit 匹配的工具链。
4. 在 CMake Profiles 中指定 Qt 6.12.0 的 `CMAKE_PREFIX_PATH`，按上述说明配置独立安装的
    ShaderTools、MQTT、SerialPort、Protobuf 和 FFmpeg SDK。
5. 升级 Qt 时选择新的构建目录，避免旧 DLL / 插件优先加载；生成器使用本机可用的 Ninja 或 Makefiles。
6. 重新加载 CMake，运行 `PIPClient` 配置。

## 使用 Qt Creator

1. 启动 Qt Creator，选择 `打开项目`。
2. 打开根目录的 `CMakeLists.txt`。
3. 选择已安装的 Qt 6.12.0 kit 和对应编译器；旧 Qt Creator 用户配置需要重新选择 kit。
4. 配置构建目录，使用 `build/windows/qt612-debug` 或 `build/linux/qt612-debug`。
5. 配置完成后运行项目。

两个 IDE 使用同一套 CMake 工程，不要分别维护项目文件。

## 构建提示

`Could NOT find WrapVulkanHeaders` 是 Qt 对可选 Vulkan 头文件的探测提示；
当前 D3D11/OpenGL 路径不以 Vulkan SDK 为前提。`GuiPrivate` 的版本绑定警告来自项目使用的
Qt 私有 RHI API，应遵守同版 kit 约束；该警告本身不代表构建失败。
首次构建或 CMake 重配可能先执行 QML 导入扫描，随后才显示 Ninja 编译进度。
判断结果应查看最终退出码及 `FAILED` / `error`，不要仅凭以上提示判断失败。
