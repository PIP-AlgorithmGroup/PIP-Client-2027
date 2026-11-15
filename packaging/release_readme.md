# PIP-Client v0.1.0-alpha.2

本包为预览版，用于本地与队内测试。欢迎页、会话配置、MQTT、UDP HEVC 图传和诊断已接入；
RMUC 比赛页、比赛字段 UI 与键鼠控制尚未完成，未完成真实裁判系统联调与完整硬件矩阵验收。

Windows 解压后运行 `bin/PIPClient.exe`，或使用 Setup 安装；不需要安装 Qt 或开发工具。
Ubuntu 使用 `sudo apt install ./PIPClient-0.1.0-alpha.2-Ubuntu-amd64.deb`，
从应用菜单或执行 `pip-client` 启动。DEB 的依赖由 apt 安装；显卡驱动由系统提供。

进入 RMUL 后按 Esc 配置会话和图传。UDP 默认端口为 3334；监听地址须匹配实际网卡。
串口设置可接入自定义控制器，默认921600/8N1/无流控，自动将有效0x0311数据转发为MQTT。
数据段最多60字节、完整帧最多69字节；本版修正75Hz发送节拍、后台计时和软件排队延迟。
调试发送/解码工具随源码的tools目录提供，需Python/Tk及requirements_serial.txt中的依赖。
首次访问局域网时按系统提示允许应用访问所需网络。软件不自动更改系统防火墙或驱动。

配置和日志保存在当前用户的数据目录，升级与卸载不主动清除。
日志入口：欢迎页诊断 / Esc 运行诊断。反馈时附版本、系统、设备与对应会话日志；
分享日志或崩溃转储前检查其中的设备、网络及进程内存信息。

Windows 包未代码签名。Linux 包须以发行说明列出的实测范围为准；
测试通过不代表所有显卡、显示环境或持续运行性能已经验收。
随包许可证见 `share/PIPClient/licenses`，依赖来源见 `third_party_notices.md`。
