#pragma once

#include <google/protobuf/message.h>

#include <QMap>
#include <QString>

/** 官方消息规则；静态存储期，不拥有消息或网络资源，可跨线程只读访问。 */
struct ProtocolRule {
    QString topic;  ///< 精确官方名称，不含 package 或机器人前缀。
    const google::protobuf::Message* prototype = nullptr;  ///< 生成代码的只读默认实例。
    bool is_outgoing = false;                              ///< true 为自定义终端发布。
    bool is_stream = false;       ///< 接收时保留每条事件/流消息，而非仅最新快照。
    int minimum_interval_us = 0;  ///< 发布最短间隔，微秒；地图还受操作角色约束。
    quint8 qos = 1;               ///< 工程发布选择，绝不超过官方最高 QoS 1。
};

/** 返回官方规则；雷达外层未确认及辅助结构返回 nullptr；无 I/O，线程安全。 */
const ProtocolRule* protocolRule(const QString& topic);
/** 返回已确认的全部官方接收绑定；无业务 UI 映射，也不包含雷达歧义外层。 */
QMap<QString, QString> officialReceiveBindings();
/**
 * 校验权威文档已明确的字段范围/长度；返回空表示通过，不推定缺省字段为零。
 * expected_robot_id 非零时校验 RobotStaticStatus 身份；无 I/O，可在调用线程执行。
 * 未知未来字段由 Protobuf 保留；未定义单位/枚举范围不自行补造。
 */
QString validateProtocolMessage(const google::protobuf::Message& message,
                                quint32 expected_robot_id = 0);
