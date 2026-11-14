# 预览版发行流程

版本源为 `cmake/version.cmake`：数字版本用于 CMake/PE，完整版本用于程序、日志、
安装器和包名。Git 标签在完整版本前加 `v`；Debian 用 `~alpha.1` 表示早于正式版。
发布时固定一个干净提交，Windows 与 Ubuntu 使用各自独立的 RelWithDebInfo 构建目录。

## 构建与部署

先用本机兼容 ABI 的 Qt / Qt MQTT 6.12.0、Protobuf 3.20.3、FFmpeg 7 SDK 配置工程，
例如在 `build/windows/alpha1-release` 或 `build/linux/alpha1-release` 指定：

```sh
cmake -S . -B <构建目录> -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=<Qt前缀> -DPIP_MQTT_ROOT=<MQTT前缀> -DPIP_PROTOBUF_ROOT=<Protobuf前缀> -DPIP_FFMPEG_ROOT=<FFmpeg前缀> -DPIP_RELEASE_LICENSE_DIR=<已收集的依赖许可证目录>
cmake --build <构建目录> --parallel
cmake --build <构建目录> --target all_qmllint
ctest --test-dir <构建目录> --output-on-failure
cmake --install <构建目录> --prefix <暂存目录>
```

发行前收集实际依赖的完整许可证、对应源码、源码校验值、官方 SBOM 与构建选项。
`PIP_RELEASE_LICENSE_DIR` 的内容统一安装到 `share/PIPClient/licenses`，
项目许可、字体、图标与 FFmpeg 声明另由 CMake 安装。使用 Qt MQTT 的 GPL 组合发行
保留原项目 Apache 声明，同时提供 GPL 文本和对应源码；不得把发行包全部描述为 Apache。

构建树保留未剥离产物；从最终部署文件中提取匹配调试符号后，仅剥离用户包的调试信息。
符号文件、未剥离程序、对应提交和依赖清单归档在忽略的 `build/release-symbols/<版本>`，
不得用后续重编译的符号代替。安装器与 ZIP 使用同一份最终部署目录。

## Windows

使用 Inno Setup 编译 CMake 生成的模板；本轮工具为 7.1.1，工具自身许可需符合使用场景。
工具不属于客户端运行依赖，下载版本应固定并检查发布者签名。

```text
ISCC /DPayloadDir=<暂存目录> /DOutputDir=<发行输出> <构建目录>/packaging/installer.iss
cpack --config <构建目录>/CPackConfig.cmake -G ZIP -B <发行输出>
```

CPack 会重新执行安装规则；如果已经提取/剥离部署目录的符号，可直接将该部署目录压缩为
以 `PIPClient-<版本>-Windows-x86_64` 为顶层目录的 ZIP，避免不小心重新装入未剥离程序。
设置固定 AppId，默认安装到当前用户目录，不要求管理员权限；桌面快捷方式可选。
卸载仅移除安装器拥有的文件，不清理设置与日志。发行检查覆盖安装、覆盖升级、卸载、
卸载注册项、用户文件保留及隔离开发环境 PATH 的原生启动。

## Ubuntu

从完整安装目录封装 DEB，所有写操作限制在输出目录；脚本不修改系统安装数据库：

```sh
python3 tests/packaging/package_linux_test.py
python3 scripts/package_linux.py --payload <暂存目录> --output <发行输出> --version 0.1.0-alpha.1
```

程序私有库位于 `/opt/pip-client`，桌面入口和图标放在 `/usr/share`，
`/usr/bin/pip-client` 提供命令入口。系统图形库、VAAPI 和显卡驱动不复制到私有目录。
DEB 显式声明 Ubuntu 22.04 基线依赖，用户通过 `apt install ./文件.deb` 解析安装。
脚本保留 SONAME 符号链接，拒绝跳出应用目录的链接，首次打包后拒绝复用工作树以避免混入旧文件。
WSL 下输出工作树必须放在支持 POSIX 权限的 Linux 文件系统，例如 `/tmp`；
未启用 metadata 的 Windows 挂载目录可能始终报告 0777，导致 dpkg-deb 拒绝控制目录。
完成后仅将生成的 `.deb` 复制回发行目录；不要修改系统挂载配置来绕过权限要求。

Linux 部署显式包含 XCB、Wayland 与用于隔离验证的 offscreen 插件，
避免 Qt SDK 默认部署仅携带 XCB。
验证 `dpkg-deb -f/-c/-x`、桌面文件、库闭包和解包后的启动。原生 X11/Wayland、实际 GPU、
网络与长期运行须分别记录；WSL 离屏验证不能替代原生 Ubuntu 硬件验收。

## 发布附件

产物放在 `build/releases/v<版本>`：Windows ZIP/Setup、Ubuntu DEB、项目源码、
依赖源码及来源清单、SHA256SUMS、发行说明和验证记录。先完成验证再创建 Git 标签和
GitHub prerelease，保留仓库原有可见性。发布后核对标签提交与远端附件大小/校验清单。
第一次预览发行不实现自动更新；用户下载新包覆盖安装。
