# 通用数据链路

## 当前边界

当前已实现 Qt MQTT 接收 → Protobuf 完整解码 → 不可变快照 → GUI/QML 通知。
启动应用只显示空白窗口，没有默认连接、订阅列表、比赛字段、HUD 或地图设计。
官方 proto 保留完整定义，应用没有把其中任何字段映射成界面属性。
发送指令、按字段做业务校验、配置编辑器和正式比赛会话尚未实现。

入口为 [DataPipeline](../src/core/data_pipeline.h)，应用通过
[main.cpp](../src/main.cpp) 把对象注入 [Main.qml](../ui/Main.qml) 的 dataPipeline 属性。
QML 暂不绑定任何状态或业务字段。Main.qml 仅保留窗口尺寸、标题和数据入口。

## 后续接入

后续由 C++ 应用协调层在获得明确配置后构造 MqttSettings，再调用 DataPipeline::start。
当前 main.cpp 不调用 start；不会因为打开窗口而连接比赛网络。
配置中的 host、client_id 和 bindings 默认全部为空，bindings 是精确 topic 到完整接收消息类型的映射。
后续比赛配置应使用官方指令名作为 topic；本地测试中的 test/input 不属于产品配置。
收到更新后，C++ 使用 snapshot(topic) 取得不可变 Protobuf 消息；
业务层自行决定映射哪些字段，再通过业务模型交给 QML。此处没有预设该模型。

QML 目前只能读取通用 connectionState、lastError，并接收 messageUpdated(topic)、
snapshotsReset 等信号；原始快照不暴露给 QML，QML 不承担协议解析。

## 配置及资源约束

| 配置或约束 | 默认值与范围 | 说明 |
| --- | --- | --- |
| host | 空，启用时必须填写 | broker 主机名/IP，不能含首尾空白或 NUL |
| client_id | 空，启用时必须填写 | 不推断机器人 ID 与选手端 ID 的转换 |
| port | 3333，范围 1–65535 | 官方端口；测试使用本地临时端口 |
| bindings | 空，启用时 1–64 项 | topic 必须精确匹配，不支持通配符和共享订阅 |
| 消息类型 | 无默认项 | 仅接收 proto 内明确的顶层消息；拒绝发送消息和雷达歧义定义 |
| MQTT 版本 | 3.1.1 | 工程选择，不作为 RoboMaster 文档新增要求 |
| QoS | 订阅请求为 1 | 不发送业务指令 |
| clean session | true | 每次重连重新订阅，等待全部 SUBACK 后才 ready |
| keep alive | 10 秒 | Qt MQTT 维护心跳 |
| 连接及订阅超时 | 每次尝试合计 5000 ms | 超时中止 socket，进入重连 |
| 重连间隔 | 1 秒起，倍增到 30 秒 | 成功订阅后重置；基于 Qt 事件循环定时器 |
| 单条已接收负载上限 | 1 MiB | 工程解析上限，超过则拒绝；不是官方业务字节上限 |
| GUI 待处理队列 | 每 topic 最多一份，最多 64 项 | 合并为最新快照，最多一个待处理通知 |

负载上限在 Qt MQTT 完成收包之后检查，不限制第三方库组包过程占用的内存。
当前接收入口没有实现官方数组长度、取值范围、CustomByteBlock 的 300 字节等业务校验。
这些校验应与后续字段映射一起加入业务边界，不能把 Protobuf 解析成功视为业务数据有效。

快照入口只适合展示最新状态，不保证完整保存每个事件或重复 MQTT 消息。
事件日志、业务去重、顺序与时间戳语义需要后续明确设计；原协议消息没有统一序列号，
此处不推断时间先后。断线立即使快照失效；格式错误保留上一份有效快照并报告错误，
不更新对应 topic。状态快照本身不推断超时，业务层以后再决定各字段的时效要求。

## 线程与生命周期

DataPipeline 的公开方法、属性、信号和快照表仅在其所属线程使用，应用中为 GUI 线程。
工作线程独占 MQTT 客户端、socket、定时器和 Protobuf 解码。共享邮箱由互斥量保护，
只传递不可变 shared_ptr 消息；GUI 批次交换后发射通知。

stop 会先隔离旧会话通知，在工作线程中 abort socket、释放客户端，再 quit/wait。
避免 Qt MQTT graceful disconnect 的发送等待阻塞界面退出。
工作对象在 QThread::finished 时释放；QML engine 先于数据入口析构，数据入口先于应用析构。
错误不记录 payload；lastError 合并保留最近一次错误，不在高频路径逐包写日志。

状态为 offline、connecting、subscribing、ready、reconnecting。
start 返回 true 仅表示异步启动，ready 才表示 broker 连接且所有订阅已确认。
无效设置会先终止旧会话，报告错误并保持 offline。

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
测试还在 offscreen 平台创建实际 Main.qml，断言没有可视子控件，
并使用测试专用 QML 对象确认更新信号可被 QML 接收。

正常构建后运行 ctest --test-dir <构建目录> --output-on-failure。
这套检查不依赖真实比赛服务器，尚未完成现场 broker、实机网络或长时间压力验证。
