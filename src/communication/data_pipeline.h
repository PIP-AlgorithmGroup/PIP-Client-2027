#pragma once

#include <google/protobuf/message.h>

#include <QByteArray>
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
struct CustomControlChannelState;

/**
 * 串口线程到既有MQTT工作线程的有界入口；不拥有网络连接，不依赖GUI派发或结果反馈。
 * submit/diagnostics可由任意线程调用；cancel仅由创建通道的GUI线程调用。
 * 通道可在DataPipeline停止/析构后保留，但此时提交只返回false，不能自动跨会话复用。
 */
class CustomControlChannel {
   public:
    /**
     * 非阻塞复制0..60字节原始数据，编码为官方CustomControl；true仅表示本地接受。
     * received_at_us为同机steady_clock微秒，TTL固定从该时刻起100ms；拒绝未来/过期时刻。
     * 共用32项/64KiB提交队列，有余量时逐帧保留；预算耗尽时替换同源最后一项为最新值。
     * 工作线程另有最多4份新鲜控制按序限频等待，持续过载时丢最旧项；无离线缓存。
     * 名义75Hz，最多4周期的有限相位补偿且实际间隔至少10000µs；所有等待共用原TTL。
     * 无同源项可替换时返回false；暂时空队列不重置连续输入的节拍。
     */
    bool submit(const QByteArray& payload, qint64 received_at_us);
    /**
     * 使本通道永久失效并同步撤回未交给TCP的控制；可重复调用，不等待网络。
     * 仅创建线程调用，与DataPipeline的start/stop串行；不关闭独立MQTT连接。
     */
    void cancel();
    /** 任意线程可读的累计提交/发送/合并/离线/过期/失败计数；不表示机器人执行。 */
    QVariantMap diagnostics() const;

   private:
    friend class DataPipeline;
    explicit CustomControlChannel(std::shared_ptr<CustomControlChannelState> state);
    std::shared_ptr<CustomControlChannelState> state_;
};

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
     * 共享提交队列最多32项/64 KiB；成功只表示本地入队，见publishFinished。
     * TTL 约束本地排队和 QoS1 等待，已经交给 TCP 的字节不能撤回，不保证远端执行时效。
     * should_defer_custom_control仅允许CustomControl：工作线程额外保存一份最新值等待限频，
     * 被更新值替换的请求返回superseded，仍扣除原TTL；默认false保留直接限频拒绝语义。
     */
    quint64 publish(const google::protobuf::Message& message, int ttl_ms = 100,
                    bool should_defer_custom_control = false);
    /**
     * 仅所属GUI线程调用：同步请工作线程撤回尚未交给TCP的请求，成功返回true并报告cancelled。
     * 已发送、已完成或不在当前会话返回false；不等待网络，也不能撤回已交给TCP的字节。
     */
    bool cancelPublication(quint64 request_id);
    /**
     * 仅所属GUI线程调用；ready时创建当前传输会话的串口发送通道，否则返回空。
     * 断线/停止后旧通道永久拒绝，重连就绪后须重新创建；先cancel再释放串口来源。
     */
    std::shared_ptr<CustomControlChannel> createCustomControlChannel();

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
    /**
     * 本地结果sent/acknowledged/expired/rate_limited/superseded/cancelled/failed，均非执行确认。
     * sent_at_us仅sent有效，为工作线程交给本地传输后的steady_clock微秒，其他结果为0；
     * 用于同机限频，不可作为远端时间，也不能用GUI收到信号的时刻代替实际发送时刻。
     */
    void publishFinished(quint64 request_id, const QString& topic, const QString& result,
                         qint64 sent_at_us);
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
