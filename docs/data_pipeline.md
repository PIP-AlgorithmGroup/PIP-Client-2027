# 官方 MQTT 数据链路

## 接入边界

应用由 [MatchSession](../src/app/match_session.h) 管理唯一一个
[DataPipeline](../src/core/data_pipeline.h)。欢迎页不连接；用户在比赛页 ESC 的当前会话中
选择网卡、机器人 ID，保存并连接。单套配置默认本机 192.168.12.2、Broker 192.168.12.1:3333，
地址/端口可直接修改，也支持本机回环；不区分正式/开发档。完整操作和本地验收见[本地接入](local_integration.md)。

官方 Topic 与 Protobuf 生成类型由 [protocol_catalog](../src/core/protocol_catalog.h) 对应；
订阅全部已确认的接收消息，发布只接受官方发送消息类型。RadarInfoToClient 外层存在歧义，
RadarSingleRobotInfo 是辅助结构，均不开放独立订阅。负载为原始二进制 Protobuf，
不增加 request_id、时间戳、模式或统一包装。机器人 ID 采用十进制文本作为 clientID，
此文本形式仍待官方实机确认。普通 Broker 不会仅凭 clientID 隔离同名 Topic 的不同席位。

QML 读取连接、错误、计数等诊断，不获取原始负载、不解析协议、不提供任意发布入口。
比赛字段到 UI 的业务模型、真实键鼠采集和控制许可尚未接入，应用不会自动发送控制命令。

## 收发契约

- 状态消息按 Topic 保留最新不可变快照；失败不覆盖上一份有效值。
- Event、PenaltyInfo、CustomByteBlock、Buff、MapClickInfo、SentryCtrlResult 使用有界事件/流队列，
  C++ 消费者调用 takeEvents 取走批次；GUI 和工作邮箱分别最多 128 条。溢出丢弃新项并累计诊断，
  不承诺持久化、无损日志、业务去重或跨 Topic 顺序。
- receivedAt 与事件时间采用本机 steady_clock 毫秒，是接收时间，不能代表机器人生成时间。
  可选字段保持 presence；不把缺省字段自动补成零。断线、停止和身份切换清理旧快照与事件。
- 解析后校验已实现的长度、范围、配对数组和浮点有限性，如 CustomControl ≤60 字节、
  CustomByteBlock ≤300 字节、键盘位图 ≤65535，以及 RobotStaticStatus 的已提供身份。
  这是传输边界校验，不能替代比赛阶段、金币、复活资格等业务判定。
- 正式协议路径拒绝 retained 业务消息，避免把 Broker 保存的旧值当作当前状态。
- publish 只在已建立 MQTT 的当前代次接受请求，复制编码并返回本地请求号。
  0 表示本地拒绝；该号码不上网。待发最多 32 项/64 KiB，QoS1 最多 16 项等待确认，
  TCP 待写预算 64 KiB。过载拒绝或重连，不无限积压。
- 实时消息采用 QoS0，最短发送间隔 13334 微秒（不超过 75 Hz）；其余业务指令采用 QoS1，
  通常最短 100 ms。地图云台手 500 ms，其余角色保守 3000 ms；当前会话采用保守档。
  达不到间隔返回 rate_limited，不延后补发，调用方负责决定是否产生新的有效请求。
- 默认 TTL 100 ms，可选 1–1000 ms，限制本地排队和 QoS1 确认等待。
  到期丢弃未发送项；QoS1 超时中断连接，结果为 failed/执行情况未知。
  已交给 TCP 的字节不能撤回，不能保证远端动作过期，远端控制安全需另行验证。
- publishFinished 的 sent 仅表示交给本地传输，acknowledged 仅表示 Broker PUBACK；
  expired/rate_limited/failed 均非机器人执行结果。结果邮箱最多 128 条，溢出丢弃最旧结果并计数。
  stop 取消本代全部请求，不再交付其结果信号；断线后不重放待发或等待确认的命令。

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

应用使用正式 Qt MQTT 模块，不自行实现 MQTT 编码和协议解析。
版本必须与所选 Qt kit 完全一致。当前验证版本为 6.9.0，
来源为 [qt/qtmqtt](https://github.com/qt/qtmqtt)，v6.9.0 对应提交
f3ef845e68fd5887b8518582546137d4635d552a。
库源码许可为 GPL-3.0-only 或商业许可，按使用的 Qt MQTT 许可部署并保留对应声明。

可以从 Qt 安装器安装同版模块；源码安装时使用同一编译器、Qt kit 和构建配置。
例如先检出上述提交，在构建目录外配置其源码，设置：

```text
cmake -S <qtmqtt源码> -B <qtmqtt构建目录> -DCMAKE_PREFIX_PATH=<Qt-kit> -DCMAKE_INSTALL_PREFIX=<MQTT安装目录> -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build <qtmqtt构建目录>
cmake --install <qtmqtt构建目录>
cmake -S . -B <应用构建目录> -DCMAKE_PREFIX_PATH=<Qt-kit> -DPIP_MQTT_ROOT=<MQTT安装目录>
```

应用 CMake 不自动下载依赖。源码和依赖构建应放在已忽略的 build-* 目录，
不能提交二进制库或开发机专用路径。Windows 测试与运行时需将 Qt 和 MQTT 的 bin 加入 PATH。

## 验证

[集成测试](../tests/data_pipeline_test.cpp) 在回环地址启动最小 MQTT 测试对端，
覆盖握手、订阅确认/拒绝、TCP 分段、解码、坏包保留旧状态、负载限制、断线重连和停止。
测试专用的 GameStatus.current_stage 等字段只用于证明链路有效，不构成产品字段设计。
测试使用专用 QML 对象确认更新信号可被 QML 接收。
实际 Main.qml 的离线状态、模式导航、键盘、设置与背景回退由
[欢迎页交互测试](../tests/welcome_ui_test.cpp) 单独覆盖。

正常构建后运行 ctest --test-dir <构建目录> --output-on-failure。
这套检查不依赖真实比赛服务器，尚未完成现场 broker、实机网络或长时间压力验证。
