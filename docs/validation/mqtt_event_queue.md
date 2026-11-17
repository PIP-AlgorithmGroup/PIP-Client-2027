# MQTT 接收事件队列诊断

2026-11-17 对会话 `session_20261117T082701258_48872_20b90488` 的诊断确认：
产品已订阅事件/流消息，但尚未接入业务消费者。`guiEventOverflow` 增长不能据此解释为
GUI 处理速度不足，也不能作为键鼠上行丢帧计数。

## 日志与代码证据

该会话最后一份运行快照中，`receivedCount=10355`、`guiEventOverflow=9114`，
`queuedPublications=0`、`queuedBytes=0`。`receivedCount` 包括状态与事件，不能直接与
`guiEventOverflow` 相减来计算成功消费事件数。日志没有逐 Topic 的事件溢出计数，
无法仅凭约 50 条/秒的增长确认是哪一个 Topic。

[DataPipeline](../../src/communication/data_pipeline.cpp) 在工作线程解析后，将事件交给
GUI 侧最多 128 条的队列，满后丢弃新项并累计 `guiEventOverflow`。
`eventsAvailable` 只是通知，消费者必须调用 `takeEvents()` 才能清空队列。
截至本次诊断，`src` 中没有 `takeEvents()` 调用者，也没有 `eventsAvailable` 的消费连接；
[MatchSession](../../src/app/match_session.cpp) 的正常启动流程不会消费这个队列。

事件队列包括 Event、PenaltyInfo、CustomByteBlock、Buff、MapClickInfo、SentryCtrlResult。
它们不通过最新状态快照替代逐条消费。工作线程邮箱的 `eventOverflow` 与 GUI 队列的
`guiEventOverflow` 是两个不同计数，必须分别检查。

## 最小复现与单变量对照

Windows 11、Qt 6.12.0 下，在现有 MatchSession 集成测试中临时加入两组对照，使用
TestBroker 回环 MQTT、真实 MatchSession 配置/启动路径。每批注入 16 条合法 Event，
等待 GUI 收到该批接收计数后再发下一批，共 9 批，隔离工作邮箱突发积压因素。
另用 1ms GUI 定时器确认主事件循环仍能运行。

两组只切换是否连接 `eventsAvailable` 并调用 `takeEvents()`：

| 场景 | 接收数 | 消费数 | GUI 溢出数 | GUI 定时器触发数 |
| --- | --- | --- | --- | --- |
| 当前产品会话，无测试消费者 | 144 | 0 | 16 | 52 |
| 加入测试消费者 | 144 | 144 | 0 | 47 |

命令为 `match_session_test diagnoseEventConsumer`，断言两组均应零溢出。
当前产品组实际 16、预期 0，断言失败；测试消费者组通过。两组共耗时约 1.44 秒。
这证明无人消费足以稳定产生本次溢出；不能由此排除未来真实负载下的其他性能问题。
临时复现代码在诊断后移除，未作为产品修复或持续回归保留。

## 修复边界

后续应在 C++ 业务层接入事件分发，取走批次并按 Topic 更新对应业务模型，再由 QML 展示。
一次性事件与连续自定义字节流须分别明确处理、缓存和丢弃策略。尚无处理功能的消息应明确
统计为未处理，不能仅调用 `takeEvents()` 后丢弃来宣称业务已正常消费。

扩大队列只会推迟再次装满；提高键鼠发布频率不会修复下行消费缺口。
本次仅完成根因诊断与对照验证，没有接入尚未定义的比赛事件业务，也没有更改产品队列策略。
原有 `eventOverflowIsBoundedAndReported` 与
`officialEventsAndStreamDoNotCollapseIntoSnapshot` 测试覆盖传输队列边界和事件不合并语义，
不代表完整产品会话已有业务消费者。

本次 Windows 执行上述对照；未在 Ubuntu 重跑临时会话对照，不能将其计为双平台实测。
清理临时测试后，Windows 重新构建 MatchSession 测试目标；上述两个既有事件队列测试通过，
包括初始化与清理共 4 项、0 失败，耗时约 9.06 秒。
原有 MatchSession 完整 CTest 组通过，耗时约 10.69 秒。
