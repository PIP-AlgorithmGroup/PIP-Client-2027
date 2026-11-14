#include "communication/data_pipeline.h"

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
#include <optional>
#include <utility>

#include "client_to_server.pb.h"
#include "communication/protocol_catalog.h"
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
    bool should_defer_custom_control = false;
    std::shared_ptr<CustomControlChannelState> channel;
};
struct PublicationResult {
    quint64 request = 0;
    QString topic;
    QString result;
    qint64 sent_at_us = 0;  ///< 仅QoS0 sent记录本地交付后时刻；steady_clock微秒。
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
    bool is_stopped = false;           ///< mutex保护；永久失效后才能销毁worker原始指针。
    quint64 transport_generation = 0;  ///< mutex保护；断线即递增，旧串口通道不能跨重连。
};

class MqttWorker;
/** 通道存活状态由pipeline邮箱锁保护，统计由独立锁保护；锁顺序为邮箱→统计。 */
struct CustomControlChannelState {
    std::weak_ptr<PipelineMailbox> mailbox;
    MqttWorker* worker = nullptr;  ///< 只在邮箱锁确认!is_stopped后投递；stop先失效再销毁。
    quint64 transport_generation = 0;
    bool is_active = true;  ///< pipeline邮箱锁保护，cancel后永久false。
    QMutex statistics_mutex;
    quint64 submitted = 0, sent = 0, coalesced = 0, offline = 0, stale = 0, failed = 0,
            cancelled = 0;
    QString last_result;
};

namespace {
// MQTT线程完成、来源线程拒绝时均可记录；不持有统计锁回调或访问GUI。
void recordChannelResult(const std::shared_ptr<CustomControlChannelState>& channel,
                         const QString& result) {
    QMutexLocker lock(&channel->statistics_mutex);
    channel->last_result = result;
    if (result == "sent")
        ++channel->sent;
    else if (result == "superseded")
        ++channel->coalesced;
    else if (result == "offline")
        ++channel->offline;
    else if (result == "expired")
        ++channel->stale;
    else if (result == "cancelled")
        ++channel->cancelled;
    else
        ++channel->failed;
}
}  // namespace

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
        custom_timer_ = new QTimer(this);
        custom_timer_->setSingleShot(true);
        custom_timer_->setTimerType(Qt::PreciseTimer);
        connect(custom_timer_, &QTimer::timeout, this, [this] {
            if (pending_custom_.isEmpty()) return;
            const auto pending = pending_custom_.takeFirst();
            dispatchPublication(pending, true);
            scheduleCustomControl();
        });
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
        {
            QMutexLocker lock(&mailbox_->mutex);
            mailbox_->is_stopped = true;
        }
        retry_timer_->stop();
        deadline_->stop();
        safety_timer_->stop();
        discardPublications();
        destroyClient();
    }

    // 有界共享批次，工作线程唯一操作客户端；保留新鲜串口批次，过期命令不重放。
    void drainPublications() {
        QList<Publication> outgoing;
        {
            QMutexLocker lock(&mailbox_->mutex);
            outgoing.swap(mailbox_->outbound);
            mailbox_->outbound_bytes = 0;
            mailbox_->has_send_notification = false;
        }
        for (const auto& item : outgoing) dispatchPublication(item);
    }

    // 与写出和限频定时器在同一线程串行执行；只撤回尚未交付TCP的本地项。
    bool cancelPublication(quint64 request) {
        for (auto it = pending_custom_.begin(); it != pending_custom_.end(); ++it)
            if (it->request == request) {
                const auto cancelled = *it;
                pending_custom_.erase(it);
                finish(cancelled, "cancelled");
                scheduleCustomControl();
                return true;
            }
        std::optional<Publication> cancelled;
        {
            QMutexLocker lock(&mailbox_->mutex);
            for (auto it = mailbox_->outbound.begin(); it != mailbox_->outbound.end(); ++it) {
                if (it->request == request) {
                    cancelled = *it;
                    mailbox_->outbound_bytes -= it->payload.size();
                    mailbox_->outbound.erase(it);
                    break;
                }
            }
        }
        if (cancelled) finish(*cancelled, "cancelled");
        return cancelled.has_value();
    }

    // 由GUI在来源串口停止/换代后同步调用，串行取消该来源的限频槽及共享提交项。
    void cancelChannel(const std::shared_ptr<CustomControlChannelState>& channel) {
        for (auto it = pending_custom_.begin(); it != pending_custom_.end();) {
            if (it->channel == channel) {
                const auto cancelled = *it;
                it = pending_custom_.erase(it);
                finish(cancelled, "cancelled");
            } else {
                ++it;
            }
        }
        scheduleCustomControl();
        QList<Publication> cancelled;
        {
            QMutexLocker lock(&mailbox_->mutex);
            for (auto it = mailbox_->outbound.begin(); it != mailbox_->outbound.end();) {
                if (it->channel == channel) {
                    mailbox_->outbound_bytes -= it->payload.size();
                    cancelled.append(*it);
                    it = mailbox_->outbound.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (const auto& item : cancelled) finish(item, "cancelled");
    }

   signals:
    // 邮箱有新批次待 GUI 读取；不携带可变消息或跨线程 QObject。
    void mailboxReady();

   private:
    // 仅工作线程调用；一次只安排下一帧，晚唤醒也不突破官方发送间隔。
    void scheduleCustomControl() {
        if (pending_custom_.isEmpty()) {
            custom_timer_->stop();
            return;
        }
        const auto interval = protocolRule("CustomControl")->minimum_interval_us;
        const auto remaining = last_sent_.contains("CustomControl")
                                   ? interval - (nowUs() - last_sent_["CustomControl"])
                                   : 0;
        // 向上取整；到期重新检查原TTL、来源会话及发送间隔，不延长有效期。
        custom_timer_->start(remaining > 0 ? int((remaining + 999) / 1000) : 0);
    }

    // 仅MQTT线程调用。自定义控制可选择等待本地限频，GUI不参与冷却计时或到期提交。
    // 串口最多4份新鲜控制按序等待，吸收USB批量交付；通用显式等待仍为一个最新槽。
    // is_deferred表示从限频队首取出，不能再次追加到队尾而改变顺序。
    void dispatchPublication(const Publication& item, bool is_deferred = false) {
        if (item.channel) {
            bool is_current;
            {
                QMutexLocker lock(&mailbox_->mutex);
                is_current = item.channel->is_active && !mailbox_->is_stopped &&
                             item.channel->transport_generation == mailbox_->transport_generation;
            }
            if (!is_current) {
                finish(item, "cancelled");
                return;
            }
        }
        if (is_stopping_ || is_reconnecting_ || !client_ ||
            client_->state() != QMqttClient::Connected) {
            finish(item, "failed");
            return;
        }
        const auto now = nowUs();
        if (now >= item.deadline_us) {
            finish(item, "expired");
            return;
        }
        const auto* rule = protocolRule(item.topic);
        const auto interval = item.topic == "MapClickCmd" && !settings_.is_commander
                                  ? 3000000
                                  : rule->minimum_interval_us;
        const auto remaining_us =
            last_sent_.contains(item.topic) ? interval - (now - last_sent_[item.topic]) : 0;
        if (remaining_us > 0 && !item.should_defer_custom_control) {
            finish(item, "rate_limited");
            return;
        }
        if (item.topic == "CustomControl") {
            if (is_deferred && remaining_us > 0) {
                pending_custom_.prepend(item);
                scheduleCustomControl();
                return;
            }
            if (!is_deferred) {
                // 通用GUI发布保持最新值语义；串口不能覆盖正常50Hz输入中的另一份新鲜数据。
                for (auto it = pending_custom_.begin(); it != pending_custom_.end();) {
                    if (!item.channel || !it->channel) {
                        const auto replaced = *it;
                        it = pending_custom_.erase(it);
                        finish(replaced, "superseded");
                    } else {
                        ++it;
                    }
                }
                if (remaining_us > 0 || !pending_custom_.isEmpty()) {
                    // 4份待发最多约56ms限频预算；持续过载时丢掉最旧项，保留最新4份。
                    if (pending_custom_.size() >= 4)
                        finish(pending_custom_.takeFirst(), "superseded");
                    pending_custom_.append(item);
                    scheduleCustomControl();
                    return;
                }
                custom_timer_->stop();
            }
        }
        if (socket_->bytesToWrite() + item.payload.size() > 65536 || in_flight_.size() >= 16) {
            finish(item, "failed");
            reconnect("MQTT send backlog exceeded bounded budget");
            return;
        }
        const auto id =
            client_->publish(QMqttTopicName(item.topic), item.payload, rule->qos, false);
        if (id < 0) {
            finish(item, "failed");
        } else {
            const auto sent_at_us = nowUs();
            last_sent_[item.topic] = sent_at_us;
            if (rule->qos == 1)
                in_flight_.insert(id, item);
            else
                finish(item, "sent", sent_at_us);
            updateMailbox([](auto& mailbox) {
                mailbox.diagnostics["sentCount"] =
                    mailbox.diagnostics.value("sentCount").toULongLong() + 1;
            });
        }
    }

    void finish(const Publication& item, const QString& result, qint64 sent_at_us = 0) {
        if (item.channel) {
            recordChannelResult(item.channel, result);
            return;  // 串口反馈不经GUI邮箱，避免GUI阻塞反向限制发送吞吐。
        }
        updateMailbox([&](auto& mailbox) {
            if (mailbox.results.size() >= 128) {
                mailbox.results.removeFirst();
                mailbox.diagnostics["resultOverflow"] =
                    mailbox.diagnostics.value("resultOverflow").toULongLong() + 1;
            }
            mailbox.results.append({item.request, item.topic, result, sent_at_us});
        });
    }

    void discardPublications() {
        custom_timer_->stop();
        for (const auto& item : std::exchange(pending_custom_, {})) finish(item, "failed");
        QList<Publication> pending;
        {
            QMutexLocker lock(&mailbox_->mutex);
            mailbox_->can_publish = false;
            ++mailbox_->transport_generation;
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
    QTimer* custom_timer_ = nullptr;     ///< 仅MQTT线程拥有；停止/断线取消，防止旧控制跨会话重放。
    QList<Publication> pending_custom_;  ///< 串口最多4份、通用等待最多1份；原始TTL不延长。
    QMap<QString, qint64> last_sent_;
    QMap<qint32, Publication> in_flight_;
    QTimer* retry_timer_ = nullptr;
    QTimer* deadline_ = nullptr;
    QSet<QString> pending_topics_;
    int retry_delay_ms_ = kInitialRetryMs;
    bool is_stopping_ = false;
    bool is_reconnecting_ = false;
};

CustomControlChannel::CustomControlChannel(std::shared_ptr<CustomControlChannelState> state)
    : state_(std::move(state)) {}

bool CustomControlChannel::submit(const QByteArray& payload, qint64 received_at_us) {
    const auto now = nowUs();
    if (payload.size() > 60 || received_at_us < 0 || received_at_us > now) {
        recordChannelResult(state_, "invalid");
        return false;
    }
    if (now - received_at_us >= 100000) {
        recordChannelResult(state_, "expired");
        return false;
    }
    const auto mailbox = state_->mailbox.lock();
    if (!mailbox) {
        recordChannelResult(state_, "offline");
        return false;
    }
    rm_client_to_server::CustomControl message;
    message.set_data(payload.constData(), payload.size());
    Publication item{0,
                     "CustomControl",
                     QByteArray::fromStdString(message.SerializeAsString()),
                     received_at_us + 100000,
                     true,
                     state_};
    QMutexLocker lock(&mailbox->mutex);
    if (!state_->is_active || mailbox->is_stopped || !mailbox->can_publish ||
        mailbox->state != "ready" ||
        state_->transport_generation != mailbox->transport_generation) {
        recordChannelResult(state_, "offline");
        return false;
    }
    // USB可在一次read交付正常50Hz输入的两帧；有预算时必须逐帧提交，不能直接覆盖前帧。
    // 仅在队列预算耗尽时替换同源最后待发项，既保留末尾最新值，也不改变本批的时间顺序。
    auto pending = mailbox->outbound.end();
    if (mailbox->outbound.size() >= 32 || mailbox->outbound_bytes + item.payload.size() > 65536) {
        for (auto it = mailbox->outbound.begin(); it != mailbox->outbound.end(); ++it)
            if (it->channel == state_) pending = it;
    }
    const auto replaced_bytes = pending == mailbox->outbound.end() ? 0 : pending->payload.size();
    if ((pending == mailbox->outbound.end() && mailbox->outbound.size() >= 32) ||
        mailbox->outbound_bytes - replaced_bytes + item.payload.size() > 65536) {
        recordChannelResult(state_, "queue_full");
        return false;
    }
    {
        QMutexLocker statistics_lock(&state_->statistics_mutex);
        ++state_->submitted;
    }
    mailbox->outbound_bytes += item.payload.size() - replaced_bytes;
    if (pending != mailbox->outbound.end()) {
        recordChannelResult(state_, "superseded");
        *pending = std::move(item);
    } else {
        mailbox->outbound.append(std::move(item));
    }
    if (!mailbox->has_send_notification) {
        mailbox->has_send_notification = true;
        auto* worker = state_->worker;
        // 持邮箱锁投递，stop必须先获得同一锁才能失效/销毁worker；禁止解锁后再碰原始指针。
        QMetaObject::invokeMethod(
            worker, [worker] { worker->drainPublications(); }, Qt::QueuedConnection);
    }
    return true;
}

void CustomControlChannel::cancel() {
    const auto mailbox = state_->mailbox.lock();
    if (!mailbox) return;
    {
        QMutexLocker lock(&mailbox->mutex);
        if (!state_->is_active) return;
        state_->is_active = false;
        if (mailbox->is_stopped) return;
    }
    // 与DataPipeline停止在同一GUI线程串行，worker在同步调用返回前不会被所属者销毁。
    auto* worker = state_->worker;
    QMetaObject::invokeMethod(
        worker, [worker, state = state_] { worker->cancelChannel(state); },
        Qt::BlockingQueuedConnection);
}

QVariantMap CustomControlChannel::diagnostics() const {
    QMutexLocker lock(&state_->statistics_mutex);
    return {{"submittedCount", state_->submitted},  {"sentCount", state_->sent},
            {"coalescedFrames", state_->coalesced}, {"offlineDrops", state_->offline},
            {"staleDrops", state_->stale},          {"failedCount", state_->failed},
            {"cancelledCount", state_->cancelled},  {"lastResult", state_->last_result}};
}

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

quint64 DataPipeline::publish(const google::protobuf::Message& message, int ttl_ms,
                              bool should_defer_custom_control) {
    const auto topic = QString::fromStdString(message.GetDescriptor()->name());
    const auto* rule = protocolRule(topic);
    QString error;
    if (!rule || !rule->is_outgoing ||
        rule->prototype->GetDescriptor() != message.GetDescriptor() || ttl_ms < 1 ||
        ttl_ms > 1000 || message.ByteSizeLong() > 65536 ||
        (should_defer_custom_control && topic != "CustomControl")) {
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
                             nowUs() + qint64(ttl_ms) * 1000, should_defer_custom_control};
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

bool DataPipeline::cancelPublication(quint64 request_id) {
    if (!worker_ || !request_id) return false;
    auto* worker = static_cast<MqttWorker*>(worker_);
    bool is_cancelled = false;
    QMetaObject::invokeMethod(
        worker, [&] { is_cancelled = worker->cancelPublication(request_id); },
        Qt::BlockingQueuedConnection);
    return is_cancelled;
}

std::shared_ptr<CustomControlChannel> DataPipeline::createCustomControlChannel() {
    if (!mailbox_ || !worker_ || connection_state_ != "ready") return {};
    QMutexLocker lock(&mailbox_->mutex);
    if (mailbox_->is_stopped || !mailbox_->can_publish || mailbox_->state != "ready") return {};
    auto state = std::make_shared<CustomControlChannelState>();
    state->mailbox = mailbox_;
    state->worker = static_cast<MqttWorker*>(worker_);
    state->transport_generation = mailbox_->transport_generation;
    return std::shared_ptr<CustomControlChannel>(new CustomControlChannel(std::move(state)));
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
        emit publishFinished(result.request, result.topic, result.result, result.sent_at_us);
        if (generation != generation_) return;
    }
    emit diagnosticsChanged();
}

#include "data_pipeline.moc"
