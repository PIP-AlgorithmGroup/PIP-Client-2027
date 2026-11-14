# 本地第三方依赖

仓库不提交下载的依赖源码、安装目录或二进制，不在 CMake 配置时自动联网下载。

- `sources/<依赖>-<版本>/`：本地下载或解压的源码。
- `installed/Windows/<依赖>-<版本>/`：Windows SDK 安装前缀。
- `installed/Linux/<依赖>-<版本>/`：Ubuntu SDK 安装前缀。
- 依赖的新构建放在 `build/dependencies/<平台>/<依赖>-<版本>/`；旧构建快照在 `build/archive/`。

Qt / Qt MQTT 固定 6.12.0，Protobuf 固定 3.20.3，FFmpeg 使用 7.x ABI。
同一依赖的源码、编译器、Qt kit 和二进制 ABI 必须匹配，不能只依据目录名称判断兼容。
各项 `PIP_*_ROOT` 指向安装前缀；外部安装的 SDK 仍可显式配置，不要求拷入仓库。
具体选型和验证边界见[平台约束](../docs/platform_compatibility.md)与[环境准备](../README.md)。
