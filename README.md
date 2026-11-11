# PIP-Client

RoboMaster 2027 PIP 战队自定义终端。

当前协议基线为 RoboMaster 2027 通信协议 V1.0.0（2026-10-29）。项目已维护自定义终端的
Protobuf 定义、通用 MQTT 接收/解码链路、欢迎页和回归测试；比赛工作台、业务订阅和字段映射留空。
UDP 图传和串口处理尚未接入。详见
[2027 自定义终端协议](docs/protocol_2027.md)。

应用启动显示欢迎页，可选择 RMUL 高校联盟赛或 RMUC 超级对抗赛；进入后为本地赛事容器。
启动、选择与进入均不连接网络。现有官方 proto 字段保留，不代表已经设计比赛 UI。
通用入口、配置边界和后续接入方式见 [数据链路](docs/data_pipeline.md)。

欢迎页、RMUL/RMUC 模式选择与背景素材方案见
[欢迎页设计](docs/welcome_page_design.md)，实际行为、配置与验证见
[欢迎页实现](docs/welcome_page_implementation.md)。设置、诊断、帮助、关于均可使用。
Tab / Shift+Tab 移动焦点，左右键切换赛事，Space 选择，Enter 激活；F11 切换全屏，
Esc 优先关闭面板，再退出全屏。动效默认关闭，可在设置中开启。
设置提供 UI 小/中/大三档（90% / 100% / 110%，默认中）；调整窗口只重新布局，
不改变字号与按钮大小，切换赛事保留窗口尺寸/最大化状态。中文使用随包 Noto Sans SC 无衬线字体。
右侧工具使用图标，悬停或键盘聚焦可查看名称。

Windows 与 Ubuntu 为同等目标平台。后续视频、3D 与推理模块必须遵守
[平台与硬件兼容约束](docs/platform_compatibility.md)，覆盖 Intel 核显、可用 NPU 和 NVIDIA 独显。
当前验证范围以该文档的“当前状态”为准。

## 当前基础技术栈

- Windows / Ubuntu
- C++20
- CMake 3.24+
- Qt 6.5+
- Qt Quick / QML / Quick Controls 2 / Qt SVG
- Qt MQTT（与 Qt kit 同版；GPL-3.0-only 或商业许可）
- Protobuf 3.20.3（BSD-3-Clause；`protoc` 和 C++ 运行库使用相同版本）
- FFmpeg（后续接入视频模块）

## 环境准备

安装 Qt 6.5 或更高版本、Qt Quick Controls 2、Qt SVG 及同版 Qt MQTT，并选择与编译器匹配的 kit。
Windows 使用匹配的 MinGW 或 MSVC，Ubuntu 使用匹配的 GCC 或 Clang；
各平台分别构建依赖和产物，并在开发、CI 与发布中统一对应工具链。
本轮验证版本为 Qt / Qt MQTT 6.9.0。若 MQTT 单独安装，配置 `PIP_MQTT_ROOT` 为其安装前缀。

使用同一工具链安装 Protobuf 3.20.3。CMake 优先从 `third_party/protobuf-mingw` 查找；
使用其他安装位置时，设置 `PIP_PROTOBUF_ROOT` 或 `Protobuf_DIR`。生成的 C++ 文件保存在构建目录。

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
4. 在 CMake Profiles 中确认 Qt kit 路径；必要时设置 `CMAKE_PREFIX_PATH`。
5. 生成器选择本机可用的 Ninja 或 Makefiles，确认构建工具路径有效。
6. 重新加载 CMake，运行 `PIPClient` 配置。

## 使用 Qt Creator

1. 启动 Qt Creator，选择 `打开项目`。
2. 打开根目录的 `CMakeLists.txt`。
3. 选择已安装的 Qt 6 kit 和对应编译器。
4. 配置构建目录，建议使用源码目录外的 `build/` 目录。
5. 配置完成后运行项目。

两个 IDE 使用同一套 CMake 工程，不要分别维护项目文件。
