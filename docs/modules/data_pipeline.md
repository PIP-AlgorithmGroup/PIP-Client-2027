# 官方 MQTT 数据链路

## 接入边界

应用由 [MatchSession](../../src/app/match_session.h) 管理唯一一个
[DataPipeline](../../src/communication/data_pipeline.h)。欢迎页不连接；用户在比赛页 ESC 的当前会话中
选择网卡、机器人 ID，保存并连接。单套配置默认本机 192.168.12.2、Broker 192.168.12.1:3333，
地址/端口可直接修改，也支持本机回环；不区分正式/开发档。完整操作和本地验收见[本地接入](../development/local_integration.md)。

官方 Topic 与 Protobuf 生成类型由 [protocol_catalog](../../src/communication/protocol_catalog.h) 对应；
订阅全部已确认的接收消息，发布只接受官方发送消息类型。RadarInfoToClient 外层存在歧义，
RadarSingleRobotInfo 是辅助结构，均不开放独立订阅。负载为原始二进制 Protobuf，
不增加 request_id、时间戳、模式或统一包装。机器人 ID 采用十进制文本作为 clientID，
此文本形式仍待官方实机确认。普通 Broker 不会仅凭 clientID 隔离同名 Topic 的不同席位。

QML 读取连接、错误、计数等诊断，不获取原始负载、不解析协议、不提供任意发布入口。
比赛字段到 UI 的业务模型、真实键鼠采集和控制许可尚未接入。
用户在比赛页明确连接[自定义控制器串口](serial_controller.md)后，有效 0x0311 负载会自动发布为
CustomControl；无新输入、数据过期或 MQTT 离线时不发送、不补发旧控制。

## 收发契约

- 状态消息按 Topic 保留最新不可变快照；失败不覆盖上一份有效值。
- Event、PenaltyInfo、CustomByteBlock、Buff、MapClickInfo、SentryCtrlResult 使用有界事件/流队列，
  C++ 消费者调用 takeEvents 取走批次；GUI 和工作邮箱分别最多 128 条。溢出丢弃新项并累计诊断，
  不承诺持久化、无损日志、业务去重或跨 Topic 顺序。
  当前产品尚未连接事件业务消费者，持续接收会装满 GUI 队列；该现象不能直接归因于 GUI 性能，
  定位证据和后续消费接入边界见[事件队列诊断](../validation/mqtt_event_queue.md)。
- receivedAt 与事件时间采用本机 steady_clock 毫秒，是接收时间，不能代表机器人生成时间。
  可选字段保持 presence；不把缺省字段自动补成零。断线、停止和身份切换清理旧快照与事件。
- 解析后校验已实现的长度、范围、配对数组和浮点有限性，如 CustomControl ≤60 字节、
  CustomByteBlock ≤300 字节、键盘位图 ≤65535，以及 RobotStaticStatus 的已提供身份。
  这是传输边界校验，不能替代比赛阶段、金币、复活资格等业务判定。
- 正式协议路径拒绝 retained 业务消息，避免把 Broker 保存的旧值当作当前状态。
- publish 只在已建立 MQTT 的当前代次接受请求，复制编码并返回本地请求号。
  0 表示本地拒绝；该号码不上网。待发最多 32 项/64 KiB，QoS1 最多 16 项等待确认，
  TCP 待写预算 64 KiB。过载拒绝或重连，不无限积压。
- 实时消息采用 QoS0；CustomControl采用名义13334µs的75Hz节拍，键鼠保持原有最短间隔；其余业务指令采用 QoS1，
  通常最短 100 ms。地图云台手 500 ms，其余角色保守 3000 ms；当前会话采用保守档。
  达不到间隔返回 rate_limited，不延后补发，调用方负责决定是否产生新的有效请求。
  GUI调用者可显式传入`should_defer_custom_control=true`，仅适用于CustomControl：工作线程
  额外保留一份最新值等待限频；更新的有效请求替换旧槽并报告`superseded`。
  限频等待使用原始TTL，过期不发，停止/断线清空，不改变其他调用者的直接限频拒绝行为。
- 默认 TTL 100 ms，可选 1–1000 ms，限制本地排队和 QoS1 确认等待。
  到期丢弃未发送项；QoS1 超时中断连接，结果为 failed/执行情况未知。
  已交给 TCP 的字节不能撤回，不能保证远端动作过期，远端控制安全需另行验证。
- publishFinished 的 sent 仅表示交给本地传输，acknowledged 仅表示 Broker PUBACK；
  expired/rate_limited/failed 均非机器人执行结果。结果邮箱最多 128 条，溢出丢弃最旧结果并计数。
  `sent_at_us`仅在`sent`有效，为本地交付后的同机steady_clock微秒；其他结果为0。
  `cancelPublication`由所属GUI线程同步撤回尚未交付TCP的请求，成功报告`cancelled`；
  已发送或不在当前会话返回false，不等待网络，也不撤回TCP字节。
  stop 取消本代全部请求，不再交付其结果信号；断线后不重放待发或等待确认的命令。

串口控制使用`createCustomControlChannel()`创建当前ready传输会话的有界通道。
`submit`可在串口线程调用，只编码官方CustomControl并共用既有唯一MQTT连接；接收时刻为
同机steady_clock微秒，TTL固定100ms，长度0..60字节。与GUI发布共用32项/64 KiB提交队列，
有预算时逐帧提交，避免USB批量读取造成正常输入丢失；预算耗尽时替换同源最后一项为最新值。
MQTT工作线程最多保留4份新鲜串口控制，按序以名义13334µs周期发送；持续过载时丢掉最旧项，
保留最新4份，所有等待仍扣除原100ms TTL。没有同源项可替换且队列满时拒绝，不产生离线缓存。
GUI的通用显式等待发布仍保留一个最新槽，不改变其他topic的直接限频拒绝语义。
通道统计在工作线程更新，GUI读取快照，不通过逐帧结果通知决定后续发送。

CustomControl的名义相位与防突发截止点独立保存，实际相邻交付至少10000µs。
连续输入使用最多4周期的有限相位预算，短暂停顿后有限补偿，避免重新定相后永久积压。
队列暂时为空不代表输入流停止，下一批可能还在USB驱动中，不能据此丢掉预算。
超过4周期没有交付才重新定相，不积攒无限空闲额度，不生成重发帧；持续过载仍受名义75Hz限制。
这是工程调度策略，官方只写75Hz频率上限，没有规定逐包最短13334µs。
预算已满足即发送；需等待时Windows使用私有高分辨率waitable timer，Ubuntu使用QChronoTimer，
不依靠毫秒向上取整、GUI轮询或自旋。MQTT TCP设置LowDelayOption，立即交付小控制包。

通道的`cancel`仅由创建它的GUI线程调用，同步失效来源并撤回未交付TCP的项，不等待网络。
提交与停止通过同一邮箱锁保护worker生命周期；停止先使邮箱永久失效，再join/销毁worker。
断线递增传输代次，旧通道即使在重连ready后也不能复用。通道可在网络拥有者销毁后保留，
此时提交返回false；统计仍可读，不访问已销毁的QObject。

## 网络、配置与生命周期

| 项目 | 默认值/限制 | 语义 |
| --- | --- | --- |
| MQTT | 3.1.1 / TCP | 工程选择，最高 QoS1；当前无 TLS/用户名密码 |
| host / client_id | 必填，无 NUL、地址无首尾空白 | 会话从合法机器人编号生成 clientID |
| local_address | 会话必填已有 IPv4 | 外部 QTcpSocket 显式 bind，不回退到另一源地址 |
| 代理策略 | MQTT socket 直连 | 不继承系统或应用 HTTP/SOCKS 代理，不修改全局代理；重连保持直连 |
| bindings | 0–64 个精确 Topic | 发布用途允许零订阅；不支持通配符或共享订阅 |
| clean session | true | 每轮重建客户端、重新等待 SUBACK |
| keep alive | 10 秒 | 由 Qt MQTT 维护，不新增官方心跳 Topic |
| 连接/订阅超时 | 每轮共 5000 ms | 超时重新连接 |
| 重连退避 | 1 秒起，倍增至 30 秒 | 成功 ready 后重置 |
| 单条解析上限 | 1 MiB | 工程限制；Qt MQTT 收完整包后才检查，不能限制库组包内存 |
| 状态邮箱 | 每 Topic 一份/最多 64 项 | 合并通知，GUI 卡顿不逐条堆积信号 |

DataPipeline 的全部公开方法、属性和信号只在所属 GUI 线程使用。
工作线程独占 MQTT、TCP、定时器与接收校验。共享邮箱持锁交换不可变消息。
公开方法不等待网络响应；stop 会同步停止工作线程并等待退出。
外部字节适配器沿用 Qt MQTT 编码，阻止其 DISCONNECT 的 30 秒同步发送等待，
不访问私有 Qt API。适配器、socket、客户端均在工作线程按依赖顺序释放。
QML engine 先销毁，随后 MatchSession 停链路，最后释放注入的接收器和 DataPipeline。

offline、connecting、subscribing、ready、reconnecting 是本地状态。
ready 表示 Broker 连接且全部请求订阅获确认，不代表视频在线或控制被允许。
start 返回 true 仅表示异步启动；配置失败保留原会话。连接失效清空旧状态；
网络断开时不缓存控制，成功重连也不会自动产生中立帧或旧动作。
错误不记录 payload 或凭证；诊断区显示实际 TCP 本地/对端端点、收发和丢弃计数。

## Qt MQTT 依赖

MQTT工作线程存活期间由平台守卫保持后台计时策略。Windows显式允许响应Qt的定时精度请求，
避免Windows 11在窗口完全遮挡/最小化后忽略这些请求；不改变CPU优先级或官方限频。
多管线共享策略，最后一份守卫在工作定时器停止后恢复原位；失败在诊断字段
`backgroundTimerError`保留原生错误，继续使用Qt基础行为。Ubuntu不调用此Windows接口。
新输入到达时，若原等待队列已过发送截止时间，会直接处理队首，避免再次排一个零间隔定时事件。
此策略改善后台吞吐，不提供操作系统硬实时保证，实测见[串口验证](../validation/serial_controller.md)。

应用使用正式 Qt MQTT 模块，不自行实现 MQTT 编码和协议解析。
版本必须与所选 Qt kit 完全一致。当前固定版本为 6.12.0，
来源为 [qt/qtmqtt v6.12.0](https://github.com/qt/qtmqtt/tree/v6.12.0)，
已在 Windows 与 Ubuntu 构建并通过本地数据管线回归，见[迁移记录](../validation/qt612.md)。
库源码许可为 GPL-3.0-only 或商业许可，按使用的 Qt MQTT 许可部署并保留对应声明。

可以从 Qt 安装器安装同版模块；源码安装时使用同一编译器、Qt kit 和构建配置。
例如先检出上述版本标签，在构建目录外配置其源码，设置：

```text
cmake -S <qtmqtt源码> -B <qtmqtt构建目录> -DCMAKE_PREFIX_PATH=<Qt-kit> -DCMAKE_INSTALL_PREFIX=<MQTT安装目录> -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build <qtmqtt构建目录>
cmake --install <qtmqtt构建目录>
cmake -S . -B <应用构建目录> -DCMAKE_PREFIX_PATH=<Qt-kit> -DPIP_MQTT_ROOT=<MQTT安装目录>
```

应用 CMake 不自动下载依赖。依赖源码放在已忽略的 `third_party/sources/`，
安装 SDK 放在 `third_party/installed/<系统>/`，依赖构建放在 `build/dependencies/`，
不能提交二进制库或开发机专用路径。Windows 测试与运行时需将 Qt 和 MQTT 的 bin 加入 PATH。

## 验证

[集成测试](../../tests/communication/data_pipeline_test.cpp) 在回环地址启动最小 MQTT 测试对端，
覆盖握手、订阅确认/拒绝、TCP 分段、解码、坏包保留旧状态、负载限制、断线重连和停止。
测试专用的 GameStatus.current_stage 等字段只用于证明链路有效，不构成产品字段设计。
测试使用专用 QML 对象确认更新信号可被 QML 接收。
实际 Main.qml 的离线状态、模式导航、键盘、设置与背景回退由
[欢迎页交互测试](../../tests/ui/welcome_ui_test.cpp) 单独覆盖。

正常构建后运行 ctest --test-dir <构建目录> --output-on-failure。
这套检查不依赖真实比赛服务器，尚未完成现场 broker、实机网络或长时间压力验证。
