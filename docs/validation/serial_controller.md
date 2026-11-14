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

- 上述首轮 Windows 自动 CTest 的物理 USB/COM 用例显式跳过；同日后续已用真实
  COM4/COM5 验证，见文末补充。两平台均未接实体自定义控制器、官方图传设备或机器人。
  Ubuntu PTY 不验证 USB 驱动；Windows 短测也未测量线路实际速率、干扰、DTR/RTS 电平和热插拔矩阵。
- Ubuntu 原生 X11/Wayland 尚未验收；当前验证环境缺少系统 `libxcb-cursor.so.0`。
  offscreen/software 测试不证明原生显示、核显/独显驱动组合已通过。
- 未执行实体链路端到端延迟、CPU/内存长稳、权限故障和各厂商驱动矩阵实测。
  100 ms TTL 从本机完整帧读出起计；网络、机器人执行和控制器生成至接收的延迟不在此保证内。

本机原始证据保存在忽略目录 `.cache/validation/serial/`，包括两平台 CTest、专项结果、
QML 截图、构建/安装和启动记录。SDK、二进制、日志与机器路径均不进入提交。

## IDE 构建目录启动回归修正

同日用户反馈 CLion 直接运行构建目录的 PIPClient.exe，进程立即返回 `0xC0000135`。
此前安装版验证通过，未覆盖该启动入口；不能将安装版启动成功解释为 IDE 已可运行。
复现脚本读取当前 CLion 运行配置的环境，直接启动实际产品，稳定得到同样退出码；
仅补充独立 SerialPort 的 bin 搜索目录后，程序完成启动并正常退出 0，确认缺失运行库是原因。

修正 Windows 产品 POST_BUILD，连同 FFmpeg 复制实际导入的 MQTT / SerialPort DLL 到
可执行文件目录；源 Qt kit 与系统 PATH 不变。重新构建后，在原先缺少 SerialPort PATH
的 CLion 环境中重跑相同启动脚本，确认程序可启动且正常退出。另更新本机未提交的 IDE
配置，明确独立模块前缀和运行目录，避免新配置依赖旧 CMake 缓存。
本修正只改变 Windows 构建目录运行库收集，Ubuntu 的 RPATH 和安装部署路径保持原实现。

## Python 联调工具与实体链路补充

同日新增[带 UI 的模拟发送器与解码工具](../development/serial_tools.md)，默认
921600/8N1/无流控，按官方 `0x0311` 封帧，数据段保持原始字节。
本轮未修改 MockServer 仓库，使用其已有 CustomControl → 0x0311 桥接。

| 验证项 | 结果与范围 |
| --- | --- |
| Windows Python 3.12.4 / Tk 8.6 | 7 项全部通过，含真实 loop 及双窗口实体串口测试 |
| Ubuntu 22.04 / Python 3.10 / Tk 8.6 | 7 项中 6 项通过，Windows 物理 COM 用例显式跳过；Tk 经 WSLg 显示服务运行 |
| 管理面冲突回归 | 两平台各 2 项通过；只对未受理、控制状态不变的 revision 冲突刷新重试一次 |
| 产品串口专项 CTest | Windows / Ubuntu 均 2/2 组通过；本轮不重复视频/GPU全量验收 |
| 产品硬件探针 | Windows / Ubuntu 均编译通过，链接真实 SerialController / DataPipeline |
| COM4 ↔ COM5 | CH343 两只 USB 串口，VID/PID 1A86:55D3；双向各 1024 全字节二进制一致 |
| 官方帧实体对连 | 每方向各 30 帧；0/3/60 字节、CRC、序号及负载一致 |
| 实体输入 → 产品 → MQTT | COM4 发送、COM5 产品接收；20 帧 CustomControl protobuf 字段和值逐字节一致 |
| 实际 MQTT 字节 → MockServer → 实体输出 | 20 帧数据段一致；另收到 39 个 CRC 有效的 A9 53 遥控帧，混合流解析通过 |
| 连续整链路 | Python → Ubuntu PTY → 产品 → MQTT → Windows MockServer → COM4/COM5 → Python；20 帧顺序及数据段一致 |

链路四种输入为空、`00 A5 FF`、28 字节 PIP1 样例、60 字节 `00..3B`，各发送五次。
产品探针诊断记录 accepted/submitted/sent 均为20，CRC/序号/过期/离线/发送失败均为0。
双窗口实体 UI 在默认50 Hz周期下收到至少30帧样例，验证轴/按键及停止、关闭和端口释放。
曾发现隐藏窗口高频全量重绘日志卡住；已改成有界增量日志，并通过相同实体测试复验。

只有一对实体端口，两段物理链路分别验证并移交所有权；连续链路的输入段为 PTY。
两段同时全实体需要两对端口，未将本次结果表述为四端口全实体端到端通过。
本轮为功能短测，未测量长稳、端到端延迟、官方无线图传或机器人实际执行。
WSLg 的 Tk UI 通过不改变 Qt 产品原生 X11/Wayland 和 GPU 的未验结论。

验收脚本使用独立、限时 MockServer / broker 与临时配置；结束停止本轮子进程并检查监听端口释放，
用户原有 MockServer 和 broker 保持运行。原始证据在忽略目录 `.cache/validation/serial_tools/`，
最终链路记录在 `final/records.json`，产品诊断在 `final/probe.txt`，不提交日志或机器专用路径。

## Windows 发送器实际频率修正

同日用户在MockServer看到收/发约31Hz，而发送工具配置为50Hz。
首轮工具验收覆盖了数据一致性及周期收发，未断言实际频率，不能用此前通过结果证明50Hz达标。

新增 `tests/tools/serial_session_test.py` 使用实际SerialSession、真实pyserial loop和满载69字节帧，
不占用用户正在使用的实体COM；记录实际写出时刻而非UI配置值。修复前可复现：
50Hz目标实际31.69Hz，75Hz目标实际61.53Hz，均未达到目标±10%的验收线。

Windows Python 3.12.4 的 `monotonic()` 基于GetTickCount64，报告分辨率15.625ms；
`Event.wait(4ms)`实测平均15.212ms。20ms截止时间在粗粒度轮询中落到约31ms，
造成截图所示约31Hz。单独换成高精度sleep后仍复现，确认时钟源和等待方式都需要修正。

改用跨平台高精度单调 `perf_counter`（Windows QPC），等待时长按距下一次发送的剩余时间截断，
每次主动等待最长4ms；Windows Python 3.11+采用原生高分辨率sleep，3.10配对请求/释放1ms计时。
继续以实际完成发送后的新周期安排下一帧，不追赶过期帧，不因卡顿突发补发。

相同回归在Windows实测48.66/72.38Hz、Ubuntu实测49.23/73.38Hz，对应目标50/75Hz，
最短间隔分别为20.26/13.54ms与20.21/13.56ms，达到本工具±10%的短测预算。
测试验证了停止及join，不改产品串口限频或官方协议。此数值是软件回环短测，
不是实体链路、CPU长稳或实时调度保证；正在运行的旧工具需重启才能加载修复。

## 客户端限频拒绝与桥接调度修正

同日第二张截图中发送器配置50Hz，MockServer显示收/发35/31Hz，丢弃累计增加。
修正Python等待后仍存在两个独立瓶颈，不能把只修发送工具当作整链路验收。
用户实际客户端聚合日志显示每秒约50个有效串口输入，同时持续出现
`rate_limited`；对运行中broker的只读订阅测得约41.45Hz。未向该broker注入测试控制。

客户端原来的14ms GUI轮询不能约束MQTT工作线程两次实际发送之间的间隔。
线程调度抖动会把两次发送挤到13334µs以内，管线因而拒绝新包。
Linux真实PTY回归使用修复前实现复现26次发送失败；修复后同一回归为零。
现以GUI收到本地发送完成的时刻保守计算下一次提交冷却，最新值等待冷却到期再提交，
继续扣除原100ms TTL，不绕过管线75Hz上限、不增加旧帧队列或追赶补发。

MockServer原来每次定时唤醒最多下发一个串口帧；Windows粗粒度唤醒下，
周期遥控与自定义两路轮流共享约64次/秒机会，自定义输出因而约31Hz并积压过期。
隔离broker、独立50Hz生产者、满载60字节负载及遥控并行的测试，修复前收到60、
写出42、丢弃18。改为入站和写完成也触发有界公平调度后，收到60、写出60、丢弃0。
仍由原单writer写出，保留14ms最小间隔、100ms TTL和16项队列上限。

| 验证项 | 结果与边界 |
| --- | --- |
| Windows客户端串口CTest | 2/2通过；Linux PTY用例在Windows明确跳过 |
| Ubuntu客户端真实PTY→产品→MQTT | 50Hz周期输入50个69字节完整帧，50个protobuf顺序及60字节数据段一致，发送失败0 |
| 双平台MockServer回归 | core、video_link、route_monitor、bridge共4/4通过；50Hz异步内存writer回归60/60/0 |
| Ubuntu桥接原生串口 | 既有真实broker与PTY、TTL、停止和会话隔离回归继续通过 |
| Windows交付入口 | 完整客户端另名链接并正常启动/退出；MockServer另名产物经临时HTTP端口、网页访问、限时退出和端口释放检查 |

客户端频率测试使用连续Qt事件循环，避免QTRY/qWait的分段等待人为延迟冷却反馈；
测试设置独立退出期限，不占用用户实体COM。MockServer内存writer采用异步post完成，
覆盖实际写出后继续调度的路径，而非仅验证同步桩。

此次没有关闭用户运行中的客户端、发送器、MockServer或broker，也未重配COM4/5/6。
修复需重启相应程序加载；自动化结果不代表已重启用户的四端口实体链路、
官方图传设备、机器人执行或长期稳定频率已验收。
