#pragma once

#include <google/protobuf/message.h>

#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>
#include <memory>

/** 收发配置；默认不连接。正式会话由协议表构造绑定，通用绑定仅供本地工具使用。 */
struct MqttSettings {
    QString host;                     ///< broker 主机名或 IP；启用时不可为空。
    QString client_id;                ///< 显式连接身份，不推断机器人与选手端 ID 的转换。
    quint16 port = 3333;              ///< TCP 端口，1–65535；3333 为协议规定的端口。
    QMap<QString, QString> bindings;  ///< 精确 topic → 完整接收消息类型；最多 64 项，不支持通配符。
    QHostAddress local_address;       ///< 非空时显式绑定该本地 IPv4，不能回退到其他接口。
    quint32 robot_id = 0;             ///< 非零时校验接收机器人身份；不作为私有 wire 字段发送。
    bool is_official = false;         ///< 启用精确 Topic/类型和业务校验，拒绝 retained 业务消息。
    bool is_commander = false;        ///< 地图云台手间隔 0.5 秒，其他操作手保守使用 3 秒。
};

// 工作线程与 GUI 交换不可变快照的私有实现，定义不暴露在公开头文件中。
struct PipelineMailbox;

/**
 * 单 MQTT 会话的收发入口，不提供比赛字段到 QML 的映射。
 * 对象及全部公开方法只在所属线程使用（应用中为 GUI 线程）；网络和解析在独立线程运行。
 * 析构会停止工作线程并等待退出；对象必须先于 QCoreApplication 析构。
 */
class DataPipeline : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// 本地端点、收发计数、队列和溢出；只读，不代表机器人执行或生成时刻。
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)

   public:
    /** 不连接网络；parent 按 Qt 父子规则管理所有权。 */
    explicit DataPipeline(QObject* parent = nullptr);
    /** 同步关闭网络工作线程；不保留失去时效的状态。 */
    ~DataPipeline() override;

    /**
     * 验证全部设置后异步连接；失败返回 false、更新 lastError 并保留原会话。
     * 重复启动会先停止旧会话；返回 true 仅表示启动成功，不保证连接或订阅成功。
     */
    bool start(const MqttSettings& settings);
    /** 同步停止并取消本代全部请求（不再发结果信号），清除快照与通知；可重复调用。 */
    void stop();
    /** 返回 offline、connecting、subscribing、ready 或 reconnecting；零订阅连接可直接 ready。 */
    QString connectionState() const;
    /** 最近一次配置、网络、订阅或解码错误；新会话/成功连接时清除，不包含负载内容。 */
    QString lastError() const;

    using MessageSnapshot = std::shared_ptr<const google::protobuf::Message>;  ///< 不可变接收快照。
    /**
     * 返回 topic 最近一次有效快照；未收到或连接丢失后返回空指针。
     * 返回值自行延长消息生命周期；只暴露给 C++，没有业务字段到 QML 的映射。
     */
    MessageSnapshot snapshot(const QString& topic) const;
    /** 接收事件/字节流项，含本地单调接收时刻；不添加到官方负载，快照不可变。 */
    struct ReceivedMessage {
        QString topic;            ///< 官方 Topic 或测试绑定。
        MessageSnapshot message;  ///< 独立共享所有权，消费者不得修改。
        qint64 received_ms = 0;   ///< steady_clock 毫秒，不可与另一机器时钟相减。
    };
    /** 取走 GUI 侧最多 128 条事件/流消息；同线程调用，消费后队列清空。 */
    QList<ReceivedMessage> takeEvents();
    /** 返回最近有效状态接收时刻；无状态/断线返回 -1，毫秒单调时间。 */
    qint64 receivedAt(const QString& topic) const;
    /** 返回线程安全转交后的本地诊断副本，仅所属线程调用。 */
    QVariantMap diagnostics() const;
    /**
     * 提交官方发送消息，返回本地请求号；0 表示拒绝，lastError 提供原因。
     * 仅所属线程调用，立即复制编码；ttl_ms=1..1000，过期/断线不重放，默认 100 ms。
     * 最多 32 项待发、64 KiB；成功只表示本地入队，见 publishFinished。
     * TTL 约束本地排队和 QoS1 等待，已经交给 TCP 的字节不能撤回，不保证远端执行时效。
     */
    quint64 publish(const google::protobuf::Message& message, int ttl_ms = 100);

   signals:
    /** 通用连接状态发生变化；发射与接收均在对象所属线程。 */
    void connectionStateChanged();
    /** lastError 改变，不向 QML 暴露原始消息或敏感连接配置。 */
    void lastErrorChanged();
    /** 指定 topic 的最新快照已更新；高频输入可合并为一次通知，不用于完整事件记录。 */
    void messageUpdated(const QString& topic);
    /** 断线或停止清空快照，界面不得继续将之前的消息视为当前状态。 */
    void snapshotsReset();
    /** 当前有事件/流待取；无逐消息 queued signal，槽可调用 takeEvents。 */
    void eventsAvailable();
    /** 本地发送结果：sent/acknowledged/expired/rate_limited/failed；均非机器人执行确认。 */
    void publishFinished(quint64 request_id, const QString& topic, const QString& result);
    /** 合并后的本地诊断更新，仅所属线程发射。 */
    void diagnosticsChanged();

   private:
    void drainMailbox();

    // 工作对象由 QThread::finished → deleteLater 在所属线程释放；本对象负责 quit/wait。
    QThread worker_thread_;
    QObject* worker_ = nullptr;
    std::shared_ptr<PipelineMailbox> mailbox_;
    QMap<QString, MessageSnapshot> snapshots_;
    QMap<QString, qint64> received_at_;
    QList<ReceivedMessage> events_;
    QVariantMap diagnostics_;
    quint64 gui_event_overflow_ = 0;  ///< 所属线程累计，独立于 worker 邮箱，stop 清零。
    quint64 next_request_ = 0;        ///< 本地单调请求号，不作为官方消息字段。
    QString connection_state_ = "offline";
    QString last_error_;
    quint64 generation_ = 0;  // 每次 stop 递增，隔离已入队的旧会话通知。
};
