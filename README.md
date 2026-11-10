# PIP-Client

RoboMaster 2027 PIP 战队自定义终端。

当前协议基线为 RoboMaster 2027 通信协议 V1.0.0（2026-10-29）。项目已维护自定义终端的
Protobuf 定义、通用 MQTT 接收/解码链路和回归测试；UI、业务订阅和字段映射留空。
UDP 图传和串口处理尚未接入。详见
[2027 自定义终端协议](docs/protocol_2027.md)。

应用默认只打开空白窗口，不连接网络。现有官方 proto 字段保留，不代表已经设计比赛 UI。
通用入口、配置边界和后续接入方式见 [数据链路](docs/data_pipeline.md)。

## 当前基础技术栈

- Windows 11
- C++20
- CMake 3.24+
- Qt 6.5+
- Qt Quick / QML
- Qt MQTT（与 Qt kit 同版；GPL-3.0-only 或商业许可）
- Protobuf 3.20.3（BSD-3-Clause；`protoc` 和 C++ 运行库使用相同版本）
- FFmpeg（后续接入视频模块）

## 环境准备

安装 Qt 6.5 或更高版本及同版 Qt MQTT，并选择与编译器匹配的 kit。
当前工程可使用 MinGW 或 MSVC，但开发团队应统一编译器和 Qt 构建套件。
本轮验证版本为 Qt / Qt MQTT 6.9.0。若 MQTT 单独安装，配置 `PIP_MQTT_ROOT` 为其安装前缀。

使用同一工具链安装 Protobuf 3.20.3。CMake 优先从 `third_party/protobuf-mingw` 查找；
使用其他安装位置时，设置 `PIP_PROTOBUF_ROOT` 或 `Protobuf_DIR`。生成的 C++ 文件保存在构建目录。

构建后运行 `ctest --test-dir <构建目录> --output-on-failure` 检查消息定义、二进制编码和
本地 MQTT → Protobuf → QML 通知链路。Windows 运行时，Qt 与独立安装的 Qt MQTT 的
`bin` 目录均须可被 DLL 加载器找到；安装应用后需一并部署依赖。

当前机器已检测到 CMake 和 MinGW；如果 CMake 找不到 Qt，请设置 `CMAKE_PREFIX_PATH` 指向 Qt 的 kit 目录，例如：

```text
C:/Qt/6.8.*/mingw_*/
```

Qt 6.9.0 的 MinGW kit 应优先使用 Qt 配套的编译器：

```text
E:/Qt/Tools/mingw1310_64/bin/
```

不要把 Qt 6.9.0 与其他版本的 MinGW 混用，否则可能在链接 Windows GUI 入口时出现 `__imp___argc` 未定义。

工程当前路径包含 `$` 字符。普通 CMake + Ninja 项目通常可以正常处理，但 Qt 6.9 的 `qt_add_qml_module` 会生成 `qmllint` 自定义规则，其中部分路径转义存在兼容性问题，可能触发 `bad $-escape`。推荐将项目移动到不含 `$` 的路径，例如 `D:/Projects/PIP-Client`；如果暂时不移动，请在 CLion 中选择 `MinGW Makefiles` 生成器。

## 使用 CLion

1. 启动 CLion，选择 `Open`，打开项目根目录。
2. CLion 会读取根目录的 `CMakeLists.txt`。
3. 在 Toolchains 中选择 Qt 配套的 MinGW 13.1（或与 Qt 匹配的 MSVC）工具链。
4. 在 CMake Profiles 中确认 Qt kit 路径；必要时设置 `CMAKE_PREFIX_PATH`。
5. 生成器选择 `MinGW Makefiles`。
6. 重新加载 CMake，运行 `PIPClient` 配置。

## 使用 Qt Creator

1. 启动 Qt Creator，选择 `打开项目`。
2. 打开根目录的 `CMakeLists.txt`。
3. 选择已安装的 Qt 6 kit 和对应编译器。
4. 配置构建目录，建议使用源码目录外的 `build/` 目录。
5. 配置完成后运行项目。

两个 IDE 使用同一套 CMake 工程，不要分别维护项目文件。
