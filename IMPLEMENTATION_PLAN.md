## Stage 1: 75Hz 分段复现
**Goal**: 同批编号测量发送、MQTT、MockServer 和无线解码。
**Success Criteria**: 定位实际吞吐下降和丢弃位置。
**Tests**: 20 秒 COM4→COM5→MQTT→COM6→COM7 采样。
**Status**: Complete

## Stage 2: 修复截止时间等待
**Goal**: 消除发送器相位漂移、客户端毫秒取整和桥接粗粒度调度。
**Success Criteria**: 75Hz 回归通过，保持有界队列、TTL、取消和协议限频。
**Tests**: Python 实际线程、客户端批量75Hz、Mock 双路75Hz 回归。
**Status**: In Progress

## Stage 3: 双平台和实体复测
**Goal**: Windows/Ubuntu 验证并记录实际75Hz链路结果。
**Success Criteria**: 分清软件丢弃与无线损失，不修改无关计划和分支。
**Tests**: 相关构建/CTest、格式检查、实体编号采样。
**Status**: Not Started
