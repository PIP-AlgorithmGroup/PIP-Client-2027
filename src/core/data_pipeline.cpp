#include "core/data_pipeline.h"

#include <google/protobuf/descriptor.h>

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>
#include <QtMqtt/QMqttClient>
#include <functional>
#include <utility>

#include "server_to_client.pb.h"

// 仅传递不可变消息，互斥量保护批次交换；GUI 卡顿时每 topic 最多保留一份待处理快照。
struct PipelineMailbox {
    QMutex mutex;
    QMap<QString, DataPipeline::MessageSnapshot> pending;
    QString state = "offline";
    QString error;
    bool should_reset = false;
    bool has_notification = false;
};

namespace {

constexpr int kMaximumBindings = 64;
constexpr qsizetype kMaximumPayloadBytes = 1024 * 1024;
constexpr int kConnectionTimeoutMs = 5000;
constexpr int kInitialRetryMs = 1000;
constexpr int kMaximumRetryMs = 30000;

// 只允许已明确声明的接收消息；雷达外层尚有歧义，辅助结构也不能成为独立 topic。
const google::protobuf::Message* receivePrototype(const QString& name) {
    const auto* file = rm_server_to_client::GameStatus::descriptor()->file();
    for (int index = 0; index < file->message_type_count(); ++index) {
        const auto* descriptor = file->message_type(index);
        if (descriptor->full_name() == name.toStdString() &&
            descriptor->name() != "RadarInfoToClient" &&
            descriptor->name() != "RadarSingleRobotInfo") {
            return google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
        }
    }
    return nullptr;
}

QString validateSettings(const MqttSettings& settings) {
    if (settings.host.trimmed().isEmpty() || settings.host != settings.host.trimmed() ||
        settings.host.contains(QChar::Null) || settings.client_id.isEmpty() ||
        settings.client_id.contains(QChar::Null) || settings.port == 0) {
        return "MQTT host, client_id and a valid port are required";
    }
    if (settings.bindings.isEmpty() || settings.bindings.size() > kMaximumBindings) {
        return "MQTT requires 1 to 64 explicit topic bindings";
    }
    for (auto it = settings.bindings.cbegin(); it != settings.bindings.cend(); ++it) {
        if (!QMqttTopicFilter(it.key()).isValid() || it.key().contains('+') ||
            it.key().contains('#') || it.key().startsWith("$share/") ||
            it.key().contains(QChar::Null) || !receivePrototype(it.value())) {
            return "Invalid MQTT topic or unsupported receive message type: " + it.key();
        }
    }
    return {};
}

}  // namespace

/** 私有工作对象；所有 MQTT、定时器与 Protobuf 解析仅在工作线程访问。 */
class MqttWorker : public QObject {
    Q_OBJECT

   public:
    // 构造时只保存设置和共享邮箱；Qt 网络对象延迟到工作线程中创建。
    MqttWorker(MqttSettings settings, std::shared_ptr<PipelineMailbox> mailbox)
        : settings_(std::move(settings)), mailbox_(std::move(mailbox)) {}

    // 仅由 QThread::started 调用，创建网络资源并发起第一轮异步连接。
    void begin() {
        retry_timer_ = new QTimer(this);
        deadline_ = new QTimer(this);
        retry_timer_->setSingleShot(true);
        deadline_->setSingleShot(true);
        connect(retry_timer_, &QTimer::timeout, this, &MqttWorker::connectBroker);
        connect(deadline_, &QTimer::timeout, this,
                [this] { reconnect("MQTT connect/subscribe timeout (5000 ms)"); });
        connectBroker();
    }

    // 由拥有者投递到工作线程执行，先停定时器再释放网络对象。
    void stop() {
        is_stopping_ = true;
        retry_timer_->stop();
        deadline_->stop();
        destroyClient();
    }

   signals:
    // 邮箱有新批次待 GUI 读取；不携带可变消息或跨线程 QObject。
    void mailboxReady();

   private:
    void destroyClient() {
        if (!client_) {
            return;
        }
        // Qt MQTT 的 graceful disconnect 可等待发送 30 秒；退出时直接中止 socket，
        // 避免 GUI 的 join 被网络背压拖住。客户端随即释放其订阅和 socket。
        client_->disconnect(this);
        if (auto* socket = qobject_cast<QAbstractSocket*>(client_->transport())) {
            socket->abort();
        }
        delete client_;
        client_ = nullptr;
    }

    void createClient() {
        // 每次重连使用新客户端，避免 clean session 仍残留旧订阅对象或确认回调。
        destroyClient();
        client_ = new QMqttClient(this);
        client_->setHostname(settings_.host);
        client_->setPort(settings_.port);
        client_->setClientId(settings_.client_id);
        client_->setProtocolVersion(QMqttClient::MQTT_3_1_1);
        client_->setCleanSession(true);
        client_->setKeepAlive(10);
        connect(client_, &QMqttClient::disconnected, this,
                [this] { reconnect("MQTT connection lost"); });
        connect(client_, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error) {
            if (error != QMqttClient::NoError) {
                reconnect(QString("MQTT client error %1").arg(int(error)));
            }
        });
        connect(client_, &QMqttClient::connected, this, &MqttWorker::subscribeTopics);
        connect(client_, &QMqttClient::messageReceived, this,
                [this](const QByteArray& payload, const QMqttTopicName& topic) {
                    decode(topic.name(), payload);
                });
    }
    // 状态、错误与消息共用单个待处理通知，避免生产速度超过 GUI 时无限堆积 queued signals。
    void updateMailbox(const std::function<void(PipelineMailbox&)>& update) {
        bool should_notify;
        {
            QMutexLocker lock(&mailbox_->mutex);
            update(*mailbox_);
            should_notify = !mailbox_->has_notification;
            mailbox_->has_notification = true;
        }
        if (should_notify) {
            emit mailboxReady();
        }
    }

    void connectBroker() {
        if (is_stopping_) {
            return;
        }
        is_reconnecting_ = false;
        createClient();
        updateMailbox([](auto& mailbox) { mailbox.state = "connecting"; });
        deadline_->start(kConnectionTimeoutMs);
        client_->connectToHost();
    }

    void reconnect(const QString& error) {
        if (is_stopping_ || is_reconnecting_) {
            return;
        }
        is_reconnecting_ = true;
        deadline_->stop();
        if (auto* socket = qobject_cast<QAbstractSocket*>(client_->transport())) {
            socket->abort();
        }
        updateMailbox([&error](auto& mailbox) {
            mailbox.state = "reconnecting";
            mailbox.error = error;
            mailbox.pending.clear();
            mailbox.should_reset = true;
        });
        retry_timer_->start(retry_delay_ms_);
        retry_delay_ms_ = qMin(retry_delay_ms_ * 2, kMaximumRetryMs);
    }

    void subscribeTopics() {
        updateMailbox([](auto& mailbox) { mailbox.state = "subscribing"; });
        pending_topics_ = QSet<QString>(settings_.bindings.keyBegin(), settings_.bindings.keyEnd());
        for (auto it = settings_.bindings.cbegin(); it != settings_.bindings.cend(); ++it) {
            auto* subscription = client_->subscribe(QMqttTopicFilter(it.key()), 1);
            if (!subscription) {
                reconnect("MQTT subscribe failed: " + it.key());
                return;
            }
            connect(subscription, &QMqttSubscription::stateChanged, this,
                    [this, topic = it.key()](QMqttSubscription::SubscriptionState state) {
                        if (state == QMqttSubscription::Error) {
                            reconnect("MQTT subscription rejected: " + topic);
                        } else if (state == QMqttSubscription::Subscribed && !is_reconnecting_) {
                            pending_topics_.remove(topic);
                            if (pending_topics_.isEmpty()) {
                                retry_delay_ms_ = kInitialRetryMs;
                                deadline_->stop();
                                updateMailbox([](auto& mailbox) {
                                    mailbox.state = "ready";
                                    mailbox.error.clear();
                                });
                            }
                        }
                    });
        }
    }

    void decode(const QString& topic, const QByteArray& payload) {
        // 多 topic 的 retained 消息可能在最后一个 SUBACK 前到达，不能以全订阅 ready 为接收前提。
        if (is_reconnecting_ || client_->state() != QMqttClient::Connected ||
            !settings_.bindings.contains(topic)) {
            return;
        }
        if (payload.size() > kMaximumPayloadBytes) {
            updateMailbox([&topic](auto& mailbox) {
                mailbox.error = "MQTT payload exceeds engineering limit of 1048576 bytes: " + topic;
            });
            return;
        }
        // 先完整解析到新消息，成功后才发布不可变快照；失败不覆盖上一份有效状态。
        const auto* prototype = receivePrototype(settings_.bindings.value(topic));
        std::unique_ptr<google::protobuf::Message> message(prototype->New());
        if (!message->ParseFromArray(payload.constData(), int(payload.size()))) {
            updateMailbox(
                [&topic](auto& mailbox) { mailbox.error = "Invalid Protobuf payload: " + topic; });
            return;
        }
        DataPipeline::MessageSnapshot snapshot(std::move(message));
        updateMailbox([&topic, &snapshot](auto& mailbox) { mailbox.pending[topic] = snapshot; });
    }

    MqttSettings settings_;
    std::shared_ptr<PipelineMailbox> mailbox_;
    QMqttClient* client_ = nullptr;
    QTimer* retry_timer_ = nullptr;
    QTimer* deadline_ = nullptr;
    QSet<QString> pending_topics_;
    int retry_delay_ms_ = kInitialRetryMs;
    bool is_stopping_ = false;
    bool is_reconnecting_ = false;
};

DataPipeline::DataPipeline(QObject* parent) : QObject(parent) {}

DataPipeline::~DataPipeline() { stop(); }

bool DataPipeline::start(const MqttSettings& settings) {
    stop();
    const auto error = validateSettings(settings);
    if (last_error_ != error) {
        last_error_ = error;
        emit lastErrorChanged();
    }
    if (!error.isEmpty()) {
        return false;
    }
    mailbox_ = std::make_shared<PipelineMailbox>();
    auto* worker = new MqttWorker(settings, mailbox_);
    worker_ = worker;
    worker->moveToThread(&worker_thread_);
    connect(&worker_thread_, &QThread::started, worker, &MqttWorker::begin);
    connect(&worker_thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &MqttWorker::mailboxReady, this, [this, generation = generation_] {
        if (generation == generation_) {
            drainMailbox();
        }
    });
    worker_thread_.start();
    return true;
}

void DataPipeline::stop() {
    ++generation_;
    if (worker_) {
        disconnect(worker_, nullptr, this, nullptr);
        auto* worker = static_cast<MqttWorker*>(worker_);
        QMetaObject::invokeMethod(
            worker, [worker] { worker->stop(); }, Qt::BlockingQueuedConnection);
        worker_thread_.quit();
        worker_thread_.wait();
        worker_ = nullptr;
    }
    mailbox_.reset();
    snapshots_.clear();
    emit snapshotsReset();
    if (connection_state_ != "offline") {
        connection_state_ = "offline";
        emit connectionStateChanged();
    }
}

QString DataPipeline::connectionState() const { return connection_state_; }

QString DataPipeline::lastError() const { return last_error_; }

DataPipeline::MessageSnapshot DataPipeline::snapshot(const QString& topic) const {
    return snapshots_.value(topic);
}

void DataPipeline::drainMailbox() {
    const auto generation = generation_;
    QMap<QString, MessageSnapshot> pending;
    QString state;
    QString error;
    bool should_reset;
    {
        QMutexLocker lock(&mailbox_->mutex);
        pending.swap(mailbox_->pending);
        state = mailbox_->state;
        error = mailbox_->error;
        should_reset = std::exchange(mailbox_->should_reset, false);
        mailbox_->has_notification = false;
    }
    if (should_reset) {
        snapshots_.clear();
        emit snapshotsReset();
        if (generation != generation_) {
            return;
        }
    }
    if (connection_state_ != state) {
        connection_state_ = state;
        emit connectionStateChanged();
        if (generation != generation_) {
            return;
        }
    }
    if (last_error_ != error) {
        last_error_ = error;
        emit lastErrorChanged();
        if (generation != generation_) {
            return;
        }
    }
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        snapshots_[it.key()] = it.value();
        emit messageUpdated(it.key());
        if (generation != generation_) {
            return;
        }
    }
}

#include "data_pipeline.moc"
