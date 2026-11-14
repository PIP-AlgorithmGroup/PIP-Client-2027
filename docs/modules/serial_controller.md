# 自定义控制器串口接入

控制器通过系统串口连接电脑。比赛页 Esc →「串口设置」中选择或手动填写设备路径，
配置参数后「保存并连接」。启动、进入比赛页和打开设置均不自动打开串口。
「保存」会断开旧串口但不连接新设备；「断开」只停止串口，不影响 MQTT/视频。
拔出、占用、无权限或驱动拒绝参数时报告实际错误，修复后手动重新连接；不自动选择其他设备。
离开比赛页和退出应用关闭串口、清空待转发数据并等待工作线程结束。

## 配置

配置位于 Qt `AppConfigLocation/serial.json`，与 `session.json` 独立，版本为 1。
整组验证后用 QSaveFile 原子保存；损坏、未来版本、无法读取或无法写入时不覆盖原文件，
保存失败保持当前连接。损坏配置修复后需重启应用重新加载。

| 字段 | 类型 / 默认值 | 范围及约束 |
| --- | --- | --- |
| portName | 字符串 / 未选择 | 最长 1024 字符，无 NUL、首尾空白；连接前必填 |
| baudRate | 整数 / 921600 bit/s | 工程范围 1..4000000；实际支持由系统/驱动决定 |
| dataBits | 整数 / 8 | 5、6、7、8 |
| parity | 字符串 / none | none、even、odd、space、mark |
| stopBits | 字符串 / 1 | 1、1.5、2；1.5 只允许搭配 5 数据位，5 数据位不搭配 2 |
| flowControl | 字符串 / none | none、hardware（RTS/CTS）、software（XON/XOFF） |

921600/8N1/无流控是本项目与控制器的默认连接约定；官方表 1-1 的 921600 原本用于
「图传发送端↔机器人」，不能据此认定所有 USB 控制器均支持此速率。
软件流控会占用 XON/XOFF 控制字节，发送固件须与之匹配；二进制默认使用无流控。

Windows 通过 Qt 枚举 COM 设备，Ubuntu 通过 Qt 枚举 `/dev/tty*`。
允许填写 `/dev/serial/by-id/` 稳定路径或测试 PTY；保存系统路径，不保存易变列表索引。
USB 设备可能改号，刷新后须确认设备，不按 VID/PID 自动替换同型号设备。
Ubuntu 需要用户有设备读写权限及创建串口锁文件的权限，常见由 `dialout` 组管理；
应用不提权、不修改系统组或设备权限。不支持的校验/停止位组合明确失败，不静默回退。

## 串口帧与 MQTT

依据仓库内《RoboMaster 2027 机甲大师高校系列赛通信协议 V1.0.0（20261029）》
§1.1、§1.4.1、§2.2.2 和附录一，复用官方图传串口帧封装。
控制器→电脑这段固定 `cmd_id=0x0311` 是本项目与控制器固件的接入约定，
不是官方定义了新的控制器 USB 传输接口。

| 偏移（字节） | 长度（字节） | 内容 |
| --- | --- | --- |
| 0 | 1 | SOF = 0xA5 |
| 1 | 2 | 小端 data_length，只计算数据段，0..60 |
| 3 | 1 | seq，控制器逐帧递增，255 后回绕至 0 |
| 4 | 1 | 前 4 字节的 CRC8 |
| 5 | 2 | 小端 cmd_id = 0x0311，线上字节为 11 03 |
| 7 | n | 原始负载，不解释内部按钮、坐标、浮点或轴布局 |
| 7+n | 2 | 前 7+n 字节的 CRC16，小端 |

整帧长度 `n+9`，最大 69 字节。CRC8 使用 init=0xff、反射多项式 0x8c；
CRC16 使用 init=0xffff、反射多项式 0x8408，均无最终异或，等价于官方附录查表算法。
固定独立向量：负载 `00 A5 FF`、seq=255 的整帧为 `A5 03 00 FF 12 11 03 00 A5 FF CD 58`。
坏帧头、错误长度/命令字/CRC 不进入 MQTT；按字节重新同步，负载中的 0xA5 不作为帧尾。

完整有效帧剥去串口封装后，将原始字节逐字节放入
`rm_client_to_server::CustomControl.data`，通过已有唯一 DataPipeline 发布至精确 Topic `CustomControl`。
不把整个串口帧、cmd_id、seq 或 CRC 包进 Protobuf，不修改官方 proto，不另建 MQTT 连接。
空数据段允许发送 presence 为真的空 bytes。没有有效串口输入时不产生控制消息。

## 线程、时效和资源预算

- QSerialPort、解析器和超时定时器归专用工作线程独占；QML 只接触配置、状态和统计。
  工作线程不逐帧向 GUI 发 queued signal；共享邮箱持锁交换一份最新有效负载。
- Qt 串口读缓存上限 4096 字节；每次读取最多 4096 字节，解析残帧缓存最多 69 字节。
  工程残帧寿命为 100 ms，不完整数据到期丢弃；25 ms 定时器驱动空闲超时检查。
- 工程序号规则：有效帧间隔不足 100 ms 时，只接受模 256 前向步长 1..127，
  丢弃重复/逆序帧；有效帧沉默至少 100 ms 后允许重新建立序号窗口。
  该规则是客户端的过时数据处理策略，不作为官方重传机制；固件必须逐帧递增 seq。
- GUI 使用 14 ms PreciseTimer 取最新值，理论最多约 71.4 次/秒；MQTT 原有 13334 µs
  最小发送间隔再次保证不超过官方 75 Hz，使用现有实时指令 QoS0。
  定时器晚到不追赶补发。最多一个 MQTT 本地发布在途，过载时合并新输入保留最新值。
- 单帧新鲜度预算为完整帧被本机读取后的 100 ms，使用 steady_clock 单调时钟。
  MQTT TTL 扣除已经消耗的本地等待时间；GUI 卡顿后的过期数据直接丢弃。
  此时刻不是控制器生成时刻；本地预算不保证网络、机器人执行或物理动作延迟。
- MQTT 断开、重连、身份切换时清空 GUI/工作邮箱、驱动输入和解析残帧，
  工作线程确认相同清理代次前不交付数据，避免清理排队期间的旧输入穿过新连接。
  新连接只转发新收到的完整帧；离线不缓存、不重放。控制器停止输入不重复发送最后一帧。
  已经交给 TCP 的字节不可撤回，`sent` 仅表示本地传输，不表示机器人执行。

串口状态与 MQTT 转发状态独立展示，计数约 250 ms 更新；文件日志每秒记录聚合统计，
只记录状态、配置与计数，不输出高频原始负载。串口无 GPU/NPU 依赖，
核显、独显和没有硬件加速的环境使用相同通信路径。

## 依赖与验证

Qt SerialPort 精确匹配 Qt 6.12.0，源码来源 `qt/qtserialport` 的 `v6.12.0`，
提交 `beb4c887f3b886447cf86592fa65b8b9c1d6e312`。
模块可按 LGPL-3.0-only / GPL-2.0-only / GPL-3.0-only 或商业许可使用，
本项目现有 GPL Qt MQTT 的二进制组合许可和源码提供要求继续适用。
优先从 Qt 安装器安装同版模块；独立构建时使用同一 Qt kit 和兼容编译器：

```text
cmake -S <qtserialport源码> -B <依赖构建目录> -DCMAKE_PREFIX_PATH=<Qt前缀> -DCMAKE_INSTALL_PREFIX=<SerialPort安装目录> -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build <依赖构建目录>
cmake --install <依赖构建目录>
cmake -S . -B <应用构建目录> -DPIP_SERIALPORT_ROOT=<SerialPort安装目录>
```

CMake 不自动下载；Windows 独立 SDK 的 bin 必须加入测试进程 PATH。
Windows 安装部署收集实际导入的 SerialPort DLL；Ubuntu 由 Qt 部署解析 ELF 依赖。
运行 `ctest --test-dir <构建目录> --output-on-failure`，新增 serial_frame_parser、serial_controller
和真实设置页交互回归。平台结果、PTY 与真实硬件的区别见[串口验证](../validation/serial_controller.md)。
