#include "core/data_pipeline.h"

#include <google/protobuf/descriptor.h>

#include <QMutex>
#include <QMutexLocker>
#include <QNetworkProxy>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <QtMqtt/QMqttClient>
#include <chrono>
#include <functional>
#include <utility>

#include "core/protocol_catalog.h"
#include "server_to_client.pb.h"

namespace {

// Qt MQTT 6.9 的 DISCONNECT 会同步等 30 秒，且断线后仍可能尝试写入旧 transport。
// 此适配器只转发异步字节 I/O：拒绝已断开 socket 的写入，退出不等待网络，不修改 MQTT 编码。
// adapter 与 socket 都只由 worker 线程访问，socket 必须晚于 adapter 析构。
class MqttTransport : public QIODevice {
   public:
    MqttTransport(QTcpSocket& socket, QObject* parent) : QIODevice(parent), socket_(socket) {
        open(QIODevice::ReadWrite);
        connect(&socket_, &QIODevice::readyRead, this, &QIODevice::readyRead);
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        return QIODevice::bytesAvailable() + socket_.bytesAvailable();
    }
    bool waitForBytesWritten(int) override { return false; }

   protected:
    qint64 readData(char* bytes, qint64 length) override { return socket_.read(bytes, length); }
    qint64 writeData(const char* bytes, qint64 length) override {
        return socket_.state() == QAbstractSocket::ConnectedState ? socket_.write(bytes, length)
                                                                  : -1;
    }

   private:
    QTcpSocket& socket_;
};
qint64 nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
struct Publication {
    quint64 request = 0;
    QString topic;
    QByteArray payload;
    qint64 deadline_us = 0;
};
struct PublicationResult {
    quint64 request = 0;
    QString topic;
    QString result;
};
}  // namespace

// 仅传递不可变消息，互斥量保护批次交换；GUI 卡顿时每 topic 最多保留一份待处理快照。
struct PipelineMailbox {
    QMutex mutex;
    QMap<QString, DataPipeline::MessageSnapshot> pending;
    QMap<QString, qint64> received_at;
    QList<DataPipeline::ReceivedMessage> events;
    QList<Publication> outbound;
    QList<PublicationResult> results;
    QVariantMap diagnostics;
    qsizetype outbound_bytes = 0;
    bool can_publish = false;
    bool has_send_notification = false;
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
    if (settings.bindings.size() > kMaximumBindings) {
        return "MQTT permits at most 64 explicit topic bindings";
    }
    if (!settings.local_address.isNull() &&
        (settings.local_address.protocol() != QAbstractSocket::IPv4Protocol ||
         settings.local_address == QHostAddress::AnyIPv4)) {
        return "MQTT local address must be a specific IPv4 address";
    }
    const auto official = officialReceiveBindings();
    for (auto it = settings.bindings.cbegin(); it != settings.bindings.cend(); ++it) {
        if (!QMqttTopicFilter(it.key()).isValid() || it.key().contains('+') ||
            it.key().contains('#') || it.key().startsWith("$share/") ||
            it.key().contains(QChar::Null) || !receivePrototype(it.value()) ||
            (settings.is_official && official.value(it.key()) != it.value())) {
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
        safety_timer_ = new QTimer(this);
        safety_timer_->setInterval(25);
        // 发送超时检测独立于输入采集；不会轮询键鼠状态。
        connect(safety_timer_, &QTimer::timeout, this, [this] {
            for (const auto& item : in_flight_) {
                if (nowUs() >= item.deadline_us) {
                    reconnect("MQTT publish timeout; command outcome unknown");
                    break;
                }
            }
        });
        safety_timer_->start();
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
        safety_timer_->stop();
        discardPublications();
        destroyClient();
    }

    // 有界共享批次，工作线程唯一操作客户端；延迟的命令只丢弃，不追赶重放。
    void drainPublications() {
        QList<Publication> outgoing;
        {
            QMutexLocker lock(&mailbox_->mutex);
            outgoing.swap(mailbox_->outbound);
            mailbox_->outbound_bytes = 0;
            mailbox_->has_send_notification = false;
        }
        for (const auto& item : outgoing) {
            if (is_stopping_ || is_reconnecting_ || !client_ ||
                client_->state() != QMqttClient::Connected) {
                finish(item, "failed");
                continue;
            }
            const auto now = nowUs();
            if (now >= item.deadline_us) {
                finish(item, "expired");
                continue;
            }
            const auto* rule = protocolRule(item.topic);
            const auto interval = item.topic == "MapClickCmd" && !settings_.is_commander
                                      ? 3000000
                                      : rule->minimum_interval_us;
            if (last_sent_.contains(item.topic) && now - last_sent_[item.topic] < interval) {
                finish(item, "rate_limited");
                continue;
            }
            if (socket_->bytesToWrite() + item.payload.size() > 65536 || in_flight_.size() >= 16) {
                finish(item, "failed");
                reconnect("MQTT send backlog exceeded bounded budget");
                continue;
            }
            const auto id =
                client_->publish(QMqttTopicName(item.topic), item.payload, rule->qos, false);
            if (id < 0) {
                finish(item, "failed");
            } else {
                last_sent_[item.topic] = now;
                if (rule->qos == 1)
                    in_flight_.insert(id, item);
                else
                    finish(item, "sent");
                updateMailbox([](auto& mailbox) {
                    mailbox.diagnostics["sentCount"] =
                        mailbox.diagnostics.value("sentCount").toULongLong() + 1;
                });
            }
        }
    }

   signals:
    // 邮箱有新批次待 GUI 读取；不携带可变消息或跨线程 QObject。
    void mailboxReady();

   private:
    void finish(const Publication& item, const QString& result) {
        updateMailbox([&](auto& mailbox) {
            if (mailbox.results.size() >= 128) {
                mailbox.results.removeFirst();
                mailbox.diagnostics["resultOverflow"] =
                    mailbox.diagnostics.value("resultOverflow").toULongLong() + 1;
            }
            mailbox.results.append({item.request, item.topic, result});
        });
    }

    void discardPublications() {
        QList<Publication> pending;
        {
            QMutexLocker lock(&mailbox_->mutex);
            mailbox_->can_publish = false;
            pending.swap(mailbox_->outbound);
            mailbox_->outbound_bytes = 0;
        }
        for (const auto& item : pending) finish(item, "failed");
        const auto in_flight = std::exchange(in_flight_, {});
        for (const auto& item : in_flight) finish(item, "failed");
    }

    void destroyClient() {
        if (!client_) {
            return;
        }
        // 外部 transport/socket 仍存活时先销毁 MQTT；适配器禁止同步发送等待。
        client_->disconnect(this);
        if (socket_) {
            socket_->disconnect(this);
        }
        delete client_;
        client_ = nullptr;
        delete transport_;
        transport_ = nullptr;
        if (socket_) socket_->abort();
        delete socket_;
        socket_ = nullptr;
    }

    void createClient() {
        // 每次重连使用新客户端，避免 clean session 仍残留旧订阅对象或确认回调。
        destroyClient();
        client_ = new QMqttClient(this);
        // Qt 不拥有外部 transport；与客户端一同在此线程创建/销毁。显式 bind 锁定源 IPv4。
        socket_ = new QTcpSocket(this);
        // 席位 RJ45 必须直连所选 Broker；HTTP 代理不支持此源 IPv4 绑定路径，
        // SOCKS 也不能保证源网卡。只覆盖本 socket，不改变系统或应用的全局代理。
        socket_->setProxy(QNetworkProxy::NoProxy);
        transport_ = new MqttTransport(*socket_, this);
        client_->setTransport(transport_, QMqttClient::IODevice);
        // Qt 6.9 setTransport 仅接 readyRead/aboutToClose；先异步建立外部 TCP，
        // connected 后调用 connectToHost 才发送 MQTT CONNECT，避免丢失连接建立通知。
        connect(socket_, &QTcpSocket::connected, this, [this] { client_->connectToHost(); });
        connect(socket_, &QTcpSocket::disconnected, this,
                [this] { reconnect("MQTT TCP disconnected"); });
        connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            reconnect("MQTT TCP error: " + socket_->errorString());
        });
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
        connect(client_, &QMqttClient::messageSent, this, [this](qint32 id) {
            if (in_flight_.contains(id)) finish(in_flight_.take(id), "acknowledged");
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
        if (!settings_.local_address.isNull() && !socket_->bind(settings_.local_address, 0)) {
            reconnect("MQTT local IPv4 bind failed: " + socket_->errorString());
            return;
        }
        socket_->connectToHost(settings_.host, settings_.port);
    }

    void reconnect(const QString& error) {
        if (is_stopping_ || is_reconnecting_) {
            return;
        }
        is_reconnecting_ = true;
        deadline_->stop();
        discardPublications();
        socket_->abort();
        updateMailbox([&error](auto& mailbox) {
            mailbox.state = "reconnecting";
            mailbox.error = error;
            mailbox.pending.clear();
            mailbox.received_at.clear();
            mailbox.events.clear();
            mailbox.should_reset = true;
            mailbox.diagnostics["localAddress"] = QString{};
            mailbox.diagnostics["reconnectCount"] =
                mailbox.diagnostics.value("reconnectCount").toULongLong() + 1;
        });
        retry_timer_->start(retry_delay_ms_);
        retry_delay_ms_ = qMin(retry_delay_ms_ * 2, kMaximumRetryMs);
    }

    void subscribeTopics() {
        updateMailbox([this](auto& mailbox) {
            mailbox.can_publish = true;
            mailbox.state = "subscribing";
            mailbox.diagnostics["localAddress"] = socket_->localAddress().toString();
            mailbox.diagnostics["localPort"] = socket_->localPort();
            mailbox.diagnostics["peerAddress"] = socket_->peerAddress().toString();
            mailbox.diagnostics["peerPort"] = socket_->peerPort();
        });
        if (settings_.bindings.isEmpty()) {
            deadline_->stop();
            retry_delay_ms_ = kInitialRetryMs;
            updateMailbox([](auto& mailbox) {
                mailbox.state = "ready";
                mailbox.error.clear();
            });
            return;
        }
        pending_topics_ = QSet<QString>(settings_.bindings.keyBegin(), settings_.bindings.keyEnd());
        for (auto it = settings_.bindings.cbegin(); it != settings_.bindings.cend(); ++it) {
            auto* subscription = client_->subscribe(QMqttTopicFilter(it.key()), 1);
            if (!subscription) {
                reconnect("MQTT subscribe failed: " + it.key());
                return;
            }
            // 保留 retain 元数据，避免把 Broker 缓存的旧业务状态当作当前状态。
            connect(subscription, &QMqttSubscription::messageReceived, this,
                    [this](const QMqttMessage& message) {
                        decode(message.topic().name(), message.payload(), message.retain());
                    });
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

    void decode(const QString& topic, const QByteArray& payload, bool retained) {
        // 多 topic 的 retained 消息可能在最后一个 SUBACK 前到达，不能以全订阅 ready 为接收前提。
        if (is_reconnecting_ || client_->state() != QMqttClient::Connected ||
            !settings_.bindings.contains(topic)) {
            return;
        }
        if (settings_.is_official && retained) {
            updateMailbox([&](auto& mailbox) {
                mailbox.error = "Retained business message rejected: " + topic;
            });
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
        if (settings_.is_official) {
            const auto error = validateProtocolMessage(*message, settings_.robot_id);
            if (!error.isEmpty()) {
                updateMailbox([&](auto& mailbox) { mailbox.error = error; });
                return;
            }
        }
        const auto* rule = protocolRule(QString::fromStdString(message->GetDescriptor()->name()));
        DataPipeline::MessageSnapshot snapshot(std::move(message));
        updateMailbox([&](auto& mailbox) {
            mailbox.diagnostics["receivedCount"] =
                mailbox.diagnostics.value("receivedCount").toULongLong() + 1;
            if (settings_.is_official && rule->is_stream) {
                if (mailbox.events.size() >= 128) {
                    mailbox.error = "Receive event/stream queue overflow";
                    mailbox.diagnostics["eventOverflow"] =
                        mailbox.diagnostics.value("eventOverflow").toULongLong() + 1;
                } else
                    mailbox.events.append({topic, snapshot, nowUs() / 1000});
            } else {
                mailbox.pending[topic] = snapshot;
                mailbox.received_at[topic] = nowUs() / 1000;
            }
        });
    }

    MqttSettings settings_;
    std::shared_ptr<PipelineMailbox> mailbox_;
    QMqttClient* client_ = nullptr;
    QTcpSocket* socket_ = nullptr;
    MqttTransport* transport_ = nullptr;  ///< 外部字节适配器，先于 socket、晚于 MQTT client 释放。
    QTimer* safety_timer_ = nullptr;
    QMap<QString, qint64> last_sent_;
    QMap<qint32, Publication> in_flight_;
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
    const auto previous_generation = generation_;
    const auto error = validateSettings(settings);
    if (last_error_ != error) {
        last_error_ = error;
        emit lastErrorChanged();
    }
    // GUI 槽允许停止/启动其他会话；新的意图优先，不能在信号返回后覆盖它。
    if (previous_generation != generation_) return false;
    if (!error.isEmpty()) {
        return false;
    }
    stop();
    if (generation_ != previous_generation + 1) return false;
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
    const auto generation = ++generation_;
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
    received_at_.clear();
    events_.clear();
    diagnostics_.clear();
    gui_event_overflow_ = 0;
    const bool state_changed = connection_state_ != "offline";
    connection_state_ = "offline";
    emit snapshotsReset();
    if (generation != generation_) return;
    emit diagnosticsChanged();
    if (generation != generation_) return;
    if (state_changed) {
        emit connectionStateChanged();
    }
}

QString DataPipeline::connectionState() const { return connection_state_; }

QString DataPipeline::lastError() const { return last_error_; }
QVariantMap DataPipeline::diagnostics() const { return diagnostics_; }
qint64 DataPipeline::receivedAt(const QString& topic) const {
    return received_at_.value(topic, -1);
}
QList<DataPipeline::ReceivedMessage> DataPipeline::takeEvents() {
    return std::exchange(events_, {});
}

quint64 DataPipeline::publish(const google::protobuf::Message& message, int ttl_ms) {
    const auto topic = QString::fromStdString(message.GetDescriptor()->name());
    const auto* rule = protocolRule(topic);
    QString error;
    if (!rule || !rule->is_outgoing ||
        rule->prototype->GetDescriptor() != message.GetDescriptor() || ttl_ms < 1 ||
        ttl_ms > 1000 || message.ByteSizeLong() > 65536) {
        error = "Invalid outgoing official message or TTL/size";
    } else
        error = validateProtocolMessage(message);
    quint64 request = 0;
    bool wake = false;
    if (error.isEmpty() && mailbox_) {
        QMutexLocker lock(&mailbox_->mutex);
        if (!mailbox_->can_publish) {
            error = "MQTT is not connected; commands are never cached offline";
        } else if (mailbox_->outbound.size() >= 32 ||
                   mailbox_->outbound_bytes + qsizetype(message.ByteSizeLong()) > 65536) {
            error = "MQTT outgoing queue is full";
        } else {
            Publication item{++next_request_, topic,
                             QByteArray::fromStdString(message.SerializeAsString()),
                             nowUs() + qint64(ttl_ms) * 1000};
            mailbox_->outbound_bytes += item.payload.size();
            mailbox_->outbound.append(item);
            wake = !mailbox_->has_send_notification;
            mailbox_->has_send_notification = true;
            request = item.request;
        }
    } else if (error.isEmpty())
        error = "MQTT is offline";
    if (!error.isEmpty()) {
        last_error_ = error;
        emit lastErrorChanged();
    }
    if (wake) {
        auto* worker = static_cast<MqttWorker*>(worker_);
        QMetaObject::invokeMethod(
            worker, [worker] { worker->drainPublications(); }, Qt::QueuedConnection);
    }
    return request;
}

DataPipeline::MessageSnapshot DataPipeline::snapshot(const QString& topic) const {
    return snapshots_.value(topic);
}

void DataPipeline::drainMailbox() {
    const auto generation = generation_;
    QMap<QString, MessageSnapshot> pending;
    QMap<QString, qint64> received_at;
    QList<ReceivedMessage> incoming_events;
    QList<PublicationResult> results;
    QString state;
    QString error;
    bool should_reset;
    {
        QMutexLocker lock(&mailbox_->mutex);
        pending.swap(mailbox_->pending);
        received_at.swap(mailbox_->received_at);
        incoming_events.swap(mailbox_->events);
        results.swap(mailbox_->results);
        diagnostics_ = mailbox_->diagnostics;
        diagnostics_["queuedPublications"] = mailbox_->outbound.size();
        diagnostics_["queuedBytes"] = mailbox_->outbound_bytes;
        state = mailbox_->state;
        error = mailbox_->error;
        should_reset = std::exchange(mailbox_->should_reset, false);
        mailbox_->has_notification = false;
    }
    if (should_reset) {
        snapshots_.clear();
        received_at_.clear();
        events_.clear();
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
        received_at_[it.key()] = received_at.value(it.key(), -1);
        emit messageUpdated(it.key());
        if (generation != generation_) {
            return;
        }
    }
    for (const auto& item : incoming_events) {
        if (events_.size() >= 128) {
            ++gui_event_overflow_;
            continue;
        }
        events_.append(item);
    }
    diagnostics_["guiEventOverflow"] = gui_event_overflow_;
    if (!events_.isEmpty()) {
        emit eventsAvailable();
        if (generation != generation_) return;
    }
    for (const auto& result : results) {
        emit publishFinished(result.request, result.topic, result.result);
        if (generation != generation_) return;
    }
    emit diagnosticsChanged();
}

#include "data_pipeline.moc"
