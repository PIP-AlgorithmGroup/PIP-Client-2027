# 控制器串口接入验证

日期：2026-11-14。功能、协议和使用方式见[串口模块](../modules/serial_controller.md)。
本记录区分自动化回归、真实内核串口 I/O 与物理设备联调，不能互相替代。

## 环境与结果

| 项目 | Windows | Ubuntu |
| --- | --- | --- |
| 系统 / 架构 | Windows 11 / x86_64 | WSL Ubuntu 22.04.5 / x86_64 |
| Qt / MQTT / SerialPort | 6.12.0 动态库 | 6.12.0 动态库 |
| 项目编译器 | MinGW GCC 13.1.0 | GCC 11.4.0 |
| 完整根工程构建 | 通过 | 通过 |
| 完整 CTest | 17/17，161.19 秒 | 16/16，127.59 秒 |
| 新增协议解析测试 | 7 passed、0 failed、0 skipped，含初始化/清理 | 7 passed、0 failed、0 skipped，含初始化/清理 |
| 串口配置及 I/O 测试 | 5 passed、0 failed、1 skipped | 6 passed、0 failed、0 skipped |
| 实际串口设置 QML | 两赛事共 4 passed、0 failed、0 skipped | 两赛事共 4 passed、0 failed、0 skipped |
| 产品 all_qmllint | 通过 | 通过 |

Windows Qt SDK 的库构建标识为 GCC 15.1.0，本项目及独立 SerialPort 模块使用现有
MinGW 13.1.0，实际构建运行通过；不宣称其他编译器 ABI 组合也已验证。
Linux 使用 offscreen/software 与既有功能验证用软件 FFmpeg；HEVC 硬件输出子用例仍跳过。
完整回归之后，对最终协议边界、持续输入、端口占用和错误原因修正重跑了串口专项，
并重新构建产品、运行 QML 静态检查和定向设置页测试。完整回归数量不代表所有子用例均无跳过。

## 已覆盖行为

- 独立固定 CRC 向量、二进制数据与空负载、60 字节边界、分片/粘包/噪声、CRC 错误、
  非法长度/命令、100 ms 残帧过期、序号重复/逆序/回绕与流重置。
  CRC 正确的其他命令整帧丢弃，其负载中的完整 0x0311 帧不会被误转发。
- 默认 921600/8N1/无流控、整组配置持久化、非法参数、损坏配置、外部文件被损坏、
  原子保存失败、无效设备、原始驱动错误保留、失败不停止独立 MQTT、关闭耗时边界。
- Ubuntu 通过 `posix_openpt` 建立真实内核 PTY，产品代码使用真实 QSerialPort，
  不替换产品解析器或串口读写；对端为测试 MQTT Broker，验证 Protobuf `CustomControl.data`
  与串口数据段逐字节一致，串口封装不进入 MQTT。
- PTY 覆盖同端口占用、半包、离线不补发、GUI 阻塞后的过期丢弃、burst 保留最新值、
  持续输入的发送上限、MQTT 重连前残帧失效、拔出故障、重新连接与离页释放后再次打开。
  持续输入测试使用 1 ms 生产定时器运行约 1 秒，检查发布总数符合 13334 µs 最小间隔预算，
  且最后一份负载为最新值；该短测不作为真实设备吞吐或长期稳定性测量。
- RMUL / RMUC 的真实设置页均验证默认值、切分区保留草稿、保存不隐式连接、
  菜单重开恢复保存值、连接错误和离页断开。1024×640 截图检查状态/错误始终位于表单上方，
  参数可滚动，底部动作可达。

## 安装与部署

两平台均从正式 CMake 安装规则生成独立暂存产物，SerialPort 使用同版独立安装前缀。
Windows 安装目录包含 Qt6SerialPort.dll，文件版本为 6.12.0.0；不修改系统 Qt kit。
安装版客户端在仅保留 Windows 系统目录的 PATH 下实际启动，日志确认 Qt 6.12.0、
Direct3D 11 和完整 QML 加载；随后只向本次新建进程的窗口请求关闭，
日志记录 `shutdown_started`、`session_finished` 及退出码 0。

Ubuntu 安装目录检查主程序及 SerialPort 的 ELF 依赖，不借用开发 SDK 的动态库搜索路径。
Qt/SerialPort/ICU 由安装目录提供；原生桌面插件仍受系统显示依赖约束。
安装版在隔离 SDK 的搜索路径下以 offscreen/software 启动，日志确认 GUI 初始化及周期诊断；
该短测在 5 秒后由 timeout 结束，只验证启动，不记为正常退出或原生桌面验收。
具体未验范围见下一节。

## 未验证范围

- Windows 物理 USB/COM 的 I/O 用例显式跳过；两平台均未接实体控制器或官方服务端。
  PTY 不验证 USB 转串口芯片、921600 实际线路速率、线缆干扰、DTR/RTS 电平和热插拔驱动差异。
- Ubuntu 原生 X11/Wayland 尚未验收；当前验证环境缺少系统 `libxcb-cursor.so.0`。
  offscreen/software 测试不证明原生显示、核显/独显驱动组合已通过。
- 未执行实体链路端到端延迟、CPU/内存长稳、权限故障和各厂商驱动矩阵实测。
  100 ms TTL 从本机完整帧读出起计；网络、机器人执行和控制器生成至接收的延迟不在此保证内。

本机原始证据保存在忽略目录 `.cache/validation/serial/`，包括两平台 CTest、专项结果、
QML 截图、构建/安装和启动记录。SDK、二进制、日志与机器路径均不进入提交。
