#pragma once

#include <google/protobuf/message.h>

#include <QMap>
#include <QObject>
#include <QString>
#include <QThread>
#include <memory>

/** 通用接收配置。默认主机、身份、topic 映射为空，不创建任何比赛业务订阅。 */
struct MqttSettings {
    QString host;                     ///< broker 主机名或 IP；启用时不可为空。
    QString client_id;                ///< 显式连接身份，不推断机器人与选手端 ID 的转换。
    quint16 port = 3333;              ///< TCP 端口，1–65535；3333 为协议规定的端口。
    QMap<QString, QString> bindings;  ///< 精确 topic → 完整接收消息类型；最多 64 项，不支持通配符。
};

// 工作线程与 GUI 交换不可变快照的私有实现，定义不暴露在公开头文件中。
struct PipelineMailbox;

/**
 * MQTT → Protobuf → 状态通知的通用接收入口，不提供比赛字段或界面布局。
 * 对象及全部公开方法只在所属线程使用（应用中为 GUI 线程）；网络和解析在独立线程运行。
 * 析构会停止工作线程并等待退出；对象必须先于 QCoreApplication 析构。
 */
class DataPipeline : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

   public:
    /** 不连接网络；parent 按 Qt 父子规则管理所有权。 */
    explicit DataPipeline(QObject* parent = nullptr);
    /** 同步关闭网络工作线程；不保留失去时效的状态。 */
    ~DataPipeline() override;

    /**
     * 验证全部设置后异步连接；失败返回 false、更新 lastError 并保持 offline。
     * 重复启动会先停止旧会话；返回 true 仅表示启动成功，不保证连接或订阅成功。
     */
    bool start(const MqttSettings& settings);
    /** 同步停止当前会话，清除消息快照，并忽略旧会话已排队的通知；可重复调用。 */
    void stop();
    /** 返回 offline、connecting、subscribing、ready 或 reconnecting。ready 需全部订阅被确认。 */
    QString connectionState() const;
    /** 最近一次配置、网络、订阅或解码错误；新会话/成功连接时清除，不包含负载内容。 */
    QString lastError() const;

    using MessageSnapshot = std::shared_ptr<const google::protobuf::Message>;  ///< 不可变接收快照。
    /**
     * 返回 topic 最近一次有效快照；未收到或连接丢失后返回空指针。
     * 返回值自行延长消息生命周期；只暴露给 C++，没有业务字段到 QML 的映射。
     */
    MessageSnapshot snapshot(const QString& topic) const;

   signals:
    /** 通用连接状态发生变化；发射与接收均在对象所属线程。 */
    void connectionStateChanged();
    /** lastError 改变，不向 QML 暴露原始消息或敏感连接配置。 */
    void lastErrorChanged();
    /** 指定 topic 的最新快照已更新；高频输入可合并为一次通知，不用于完整事件记录。 */
    void messageUpdated(const QString& topic);
    /** 断线或停止清空快照，界面不得继续将之前的消息视为当前状态。 */
    void snapshotsReset();

   private:
    void drainMailbox();

    // 工作对象由 QThread::finished → deleteLater 在所属线程释放；本对象负责 quit/wait。
    QThread worker_thread_;
    QObject* worker_ = nullptr;
    std::shared_ptr<PipelineMailbox> mailbox_;
    QMap<QString, MessageSnapshot> snapshots_;
    QString connection_state_ = "offline";
    QString last_error_;
    quint64 generation_ = 0;  // 每次 stop 递增，隔离已入队的旧会话通知。
};
