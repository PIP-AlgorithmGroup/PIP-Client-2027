#include "core/data_pipeline.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include "server_to_client.pb.h"

namespace {

// 仅供测试的 MQTT 3.1.1 对端：实现握手、订阅确认和 PUBLISH 注入，不作为产品 broker。
class TestBroker : public QTcpServer {
   public:
    TestBroker() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            socket_ = nextPendingConnection();
            buffer_.clear();
            ++connection_count;
            connect(socket_, &QTcpSocket::readyRead, this, [this] {
                buffer_ += socket_->readAll();
                readPackets();
            });
        });
    }

    void publish(const QString& topic, const QByteArray& payload) {
        const auto topic_bytes = topic.toUtf8();
        QByteArray body;
        body.append(char(topic_bytes.size() >> 8));
        body.append(char(topic_bytes.size() & 255));
        body += topic_bytes;
        body += payload;
        QByteArray packet(1, char(0x30));
        auto remaining = body.size();
        do {
            auto digit = remaining % 128;
            remaining /= 128;
            packet.append(char(digit | (remaining ? 0x80 : 0)));
        } while (remaining);
        // 分两次写入以覆盖 TCP 分段，MQTT 包重组交给实际传输库。
        socket_->write(packet + body.left(1));
        const QPointer<QTcpSocket> target = socket_;
        QTimer::singleShot(1, this, [target, body] {
            if (target) {
                target->write(body.mid(1));
            }
        });
    }

    void dropConnection() { socket_->abort(); }
    bool reject_subscription = false;
    bool acknowledge_subscription = true;
    bool send_initial_message = false;
    int connection_count = 0;
    QStringList subscribed_topics;

   private:
    void readPackets() {
        while (buffer_.size() >= 2) {
            qsizetype length = 0;
            qsizetype position = 1;
            qsizetype multiplier = 1;
            unsigned char digit;
            do {
                if (position >= buffer_.size()) {
                    return;
                }
                digit = static_cast<unsigned char>(buffer_[position++]);
                length += (digit & 127) * multiplier;
                multiplier *= 128;
            } while (digit & 128);
            if (buffer_.size() - position < length) {
                return;
            }
            const auto header = static_cast<unsigned char>(buffer_[0]);
            const auto body = buffer_.mid(position, length);
            buffer_.remove(0, position + length);
            if ((header >> 4) == 1) {
                socket_->write(QByteArray::fromHex("20020000"));
            } else if ((header >> 4) == 8 && body.size() >= 5) {
                const auto topic_size = (static_cast<unsigned char>(body[2]) << 8) |
                                        static_cast<unsigned char>(body[3]);
                subscribed_topics.append(QString::fromUtf8(body.mid(4, topic_size)));
                if (acknowledge_subscription) {
                    QByteArray reply = QByteArray::fromHex("9003") + body.left(2);
                    reply.append(char(reject_subscription ? 0x80 : 1));
                    socket_->write(reply);
                    if (send_initial_message) {
                        // 在该 topic 的 SUBACK 后立即发初始状态，其余 topic 可能仍待确认。
                        const auto topic = body.mid(4, topic_size);
                        QByteArray message = QByteArray::fromHex("3100");
                        message[1] = char(2 + topic.size() + 2);
                        message.append(char(topic.size() >> 8));
                        message.append(char(topic.size() & 255));
                        message += topic;
                        message += QByteArray::fromHex("2804");
                        socket_->write(message);
                    }
                }
            } else if ((header >> 4) == 12) {
                socket_->write(QByteArray::fromHex("d000"));
            }
        }
    }

    QPointer<QTcpSocket> socket_;
    QByteArray buffer_;
};

}  // namespace

/** 使用回环网络验证完整传输链路；测试 topic 和字段仅存在于本文件，不作为应用默认配置。 */
class DataPipelineTest : public QObject {
    Q_OBJECT

   private slots:
    void blankWindowHasOnlyPipelineEntry() {
        DataPipeline pipeline;
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(PIP_MAIN_QML_PATH));
        std::unique_ptr<QObject> window(component.createWithInitialProperties(
            {{"dataPipeline", QVariant::fromValue(static_cast<QObject*>(&pipeline))}}));
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        auto* quick_window = qobject_cast<QQuickWindow*>(window.get());
        QVERIFY(quick_window != nullptr);
        QCOMPARE(quick_window->contentItem()->childItems().size(), 0);
        QCOMPARE(pipeline.connectionState(), QString("offline"));
    }

    void emptyConfigurationStaysOffline() {
        DataPipeline pipeline;
        QCOMPARE(pipeline.connectionState(), QString("offline"));
        QVERIFY(!pipeline.start(MqttSettings{}));
        QCOMPARE(pipeline.connectionState(), QString("offline"));
        QVERIFY(!pipeline.lastError().isEmpty());
        QVERIFY(!pipeline.snapshot("GameStatus"));
    }

    void invalidBindingsFailBeforeConnecting() {
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.client_id = "test";
        settings.bindings.insert("test/input", "rm_client_to_server.CustomControl");
        QVERIFY(!pipeline.start(settings));
        settings.bindings = {{"test/#", "rm_server_to_client.GameStatus"}};
        QVERIFY(!pipeline.start(settings));
        settings.bindings = {{"test/input", "rm_server_to_client.RadarInfoToClient"}};
        QVERIFY(!pipeline.start(settings));
    }

    void receiveDecodeNotifyAndReconnect() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "pipeline-test";
        settings.bindings.insert("test/input", "rm_server_to_client.GameStatus");

        QQmlEngine engine;
        QQmlComponent component(&engine);
        // 只在测试中绑定通用更新信号，产品 QML 保持空白。
        component.setData(R"(
            import QtQml
            QtObject {
                required property QtObject pipeline
                property int notices: 0
                property Connections listener: Connections {
                    target: pipeline
                    function onMessageUpdated(topic) { notices += 1 }
                }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> observer(component.createWithInitialProperties(
            {{"pipeline", QVariant::fromValue(static_cast<QObject*>(&pipeline))}}));
        QVERIFY2(observer != nullptr, qPrintable(component.errorString()));
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        QCOMPARE(broker.subscribed_topics, QStringList{"test/input"});

        broker.publish("test/input", QByteArray::fromHex("2804"));
        QTRY_COMPARE(observer->property("notices").toInt(), 1);
        const auto first = pipeline.snapshot("test/input");
        const auto* game = dynamic_cast<const rm_server_to_client::GameStatus*>(first.get());
        QVERIFY(game != nullptr);
        QVERIFY(game->has_current_stage());
        QCOMPARE(game->current_stage(), 4U);

        broker.publish("test/input", QByteArray::fromHex("2880"));
        QTRY_VERIFY(!pipeline.lastError().isEmpty());
        QCOMPARE(pipeline.snapshot("test/input").get(), first.get());
        QCOMPARE(observer->property("notices").toInt(), 1);

        broker.dropConnection();
        QTRY_COMPARE_WITH_TIMEOUT(broker.connection_count, 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        QCOMPARE(broker.subscribed_topics.size(), 2);
        QVERIFY(!pipeline.snapshot("test/input"));
        broker.publish("test/input", QByteArray::fromHex("2800"));
        QTRY_COMPARE(observer->property("notices").toInt(), 2);
        const auto second = pipeline.snapshot("test/input");
        const auto* new_game = dynamic_cast<const rm_server_to_client::GameStatus*>(second.get());
        QVERIFY(new_game && new_game->has_current_stage() && new_game->current_stage() == 0);

        pipeline.stop();
        QCOMPARE(pipeline.connectionState(), QString("offline"));
        QVERIFY(!pipeline.snapshot("test/input"));
    }

    void rejectedSubscriptionDoesNotBecomeReady() {
        TestBroker broker;
        broker.reject_subscription = true;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "rejected-test";
        settings.bindings.insert("test/input", "rm_server_to_client.GameStatus");
        QVERIFY(pipeline.start(settings));
        QTRY_VERIFY(!pipeline.lastError().isEmpty());
        QVERIFY(pipeline.connectionState() != "ready");
        pipeline.stop();
    }

    void receivesInitialMessagesDuringSubscription() {
        TestBroker broker;
        broker.send_initial_message = true;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "initial-message-test";
        settings.bindings = {{"test/a", "rm_server_to_client.GameStatus"},
                             {"test/b", "rm_server_to_client.GameStatus"}};
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        QTRY_VERIFY(pipeline.snapshot("test/a"));
        QTRY_VERIFY(pipeline.snapshot("test/b"));
    }

    void subscribeTimeoutReconnects() {
        TestBroker broker;
        broker.acknowledge_subscription = false;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "timeout-test";
        settings.bindings.insert("test/input", "rm_server_to_client.GameStatus");
        QVERIFY(pipeline.start(settings));
        QTRY_VERIFY_WITH_TIMEOUT(pipeline.lastError().contains("timeout"), 7000);
        QVERIFY(pipeline.connectionState() != "ready");
        broker.acknowledge_subscription = true;
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        QCOMPARE(broker.connection_count, 2);
    }

    void stopFromReadySignalDiscardsPendingData() {
        TestBroker broker;
        broker.send_initial_message = true;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        connect(&pipeline, &DataPipeline::connectionStateChanged, &pipeline, [&pipeline] {
            if (pipeline.connectionState() == "ready") {
                pipeline.stop();
            }
        });
        QSignalSpy states(&pipeline, &DataPipeline::connectionStateChanged);
        QSignalSpy updates(&pipeline, &DataPipeline::messageUpdated);
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "stop-on-ready";
        settings.bindings.insert("test/input", "rm_server_to_client.GameStatus");
        QVERIFY(pipeline.start(settings));
        QTRY_VERIFY(states.size() > 0);
        QTRY_COMPARE(pipeline.connectionState(), QString("offline"));
        QCOMPARE(updates.size(), 0);
        QVERIFY(!pipeline.snapshot("test/input"));
    }

    void stoppingDropsPendingUpdates() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "stop-test";
        settings.bindings.insert("test/input", "rm_server_to_client.GameStatus");
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        QSignalSpy updates(&pipeline, &DataPipeline::messageUpdated);
        broker.publish("test/input", QByteArray::fromHex("2804"));
        pipeline.stop();
        QCoreApplication::processEvents();
        QCOMPARE(updates.size(), 0);
        QCOMPARE(pipeline.connectionState(), QString("offline"));
        QVERIFY(!pipeline.snapshot("test/input"));
    }

    void stopWhileSubscribingIsBounded() {
        TestBroker broker;
        broker.acknowledge_subscription = false;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "stop-subscribing";
        settings.bindings.insert("test/input", "rm_server_to_client.GameStatus");
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("subscribing"), 5000);
        QElapsedTimer elapsed;
        elapsed.start();
        pipeline.stop();
        QVERIFY(elapsed.elapsed() < 1000);
        QCOMPARE(pipeline.connectionState(), QString("offline"));
    }

    void rejectedPayloadLimitPreservesSnapshot() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "size-test";
        settings.bindings.insert("test/input", "rm_server_to_client.CustomByteBlock");
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        broker.publish("test/input", QByteArray::fromHex("0a00"));
        QTRY_VERIFY(pipeline.snapshot("test/input"));
        const auto first = pipeline.snapshot("test/input");
        broker.publish("test/input", QByteArray(1024 * 1024 + 1, 'x'));
        QTRY_VERIFY(pipeline.lastError().contains("1048576"));
        QCOMPARE(pipeline.snapshot("test/input").get(), first.get());
    }
};

QTEST_MAIN(DataPipelineTest)
#include "data_pipeline_test.moc"
