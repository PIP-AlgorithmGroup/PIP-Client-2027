#include "communication/data_pipeline.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include "client_to_server.pb.h"
#include "communication/protocol_catalog.h"
#include "server_to_client.pb.h"
#include "support/mqtt_test_broker.h"

/** 使用回环网络验证完整传输链路；测试 topic 和字段仅存在于本文件，不作为应用默认配置。 */
class DataPipelineTest : public QObject {
    Q_OBJECT

   private slots:
    void seatConnectionIgnoresApplicationProxy_data() {
        QTest::addColumn<int>("proxy_type");
        QTest::newRow("http-proxy") << int(QNetworkProxy::HttpProxy);
        QTest::newRow("socks5-proxy") << int(QNetworkProxy::Socks5Proxy);
    }

    void seatConnectionIgnoresApplicationProxy() {
        QFETCH(int, proxy_type);
        TestBroker broker;
        QTcpServer proxy;
        broker.setProxy(QNetworkProxy::NoProxy);
        proxy.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        const auto previous_proxy = QNetworkProxy::applicationProxy();
        // 全局代理仅用于模拟用户环境；pipeline 先析构，随后恢复代理，避免污染其他测试。
        const auto restore_proxy =
            qScopeGuard([previous_proxy] { QNetworkProxy::setApplicationProxy(previous_proxy); });
        QNetworkProxy::setApplicationProxy(
            QNetworkProxy(QNetworkProxy::ProxyType(proxy_type), "127.0.0.1", proxy.serverPort()));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "103";
        settings.local_address = QHostAddress::LocalHost;
        QVERIFY(pipeline.start(settings));
        QTRY_VERIFY_WITH_TIMEOUT(
            pipeline.connectionState() == "ready" || !pipeline.lastError().isEmpty(), 1500);
        QVERIFY2(pipeline.connectionState() == "ready", qPrintable(pipeline.lastError()));
        QCOMPARE(pipeline.diagnostics().value("localAddress").toString(), QString("127.0.0.1"));
        QCOMPARE(pipeline.diagnostics().value("peerPort").toInt(), int(broker.serverPort()));
        // 重连创建新 socket，也必须维持直连策略，不能重新继承应用代理。
        broker.dropConnection();
        QTRY_VERIFY_WITH_TIMEOUT(broker.connection_count >= 2, 4000);
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QVERIFY(!proxy.hasPendingConnections());
        QCOMPARE(QNetworkProxy::applicationProxy().type(), QNetworkProxy::ProxyType(proxy_type));
    }

    void reentrantStartFromResetPreservesNewestSession() {
        TestBroker original;
        TestBroker newest;
        QVERIFY(original.listen(QHostAddress::LocalHost));
        QVERIFY(newest.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = original.serverPort();
        settings.client_id = "3";
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        bool restarted = false;
        connect(&pipeline, &DataPipeline::snapshotsReset, &pipeline, [&] {
            if (restarted) return;
            restarted = true;
            settings.port = newest.serverPort();
            QVERIFY(pipeline.start(settings));
        });
        pipeline.stop();
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QCOMPARE(newest.connection_count, 1);
        QCOMPARE(pipeline.diagnostics().value("peerPort").toInt(), int(newest.serverPort()));
        // 先断开捕获局部变量的槽，避免析构再调用已结束生命周期的引用。
        disconnect(&pipeline, &DataPipeline::snapshotsReset, &pipeline, nullptr);
    }

    void officialRetainedAndWrongDirectionAreRejected() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "3";
        settings.is_official = true;
        settings.bindings = {{"RobotStaticStatus", "rm_server_to_client.RobotStaticStatus"}};
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        broker.publish("RobotStaticStatus", QByteArray::fromHex("2003"), true);
        QTRY_VERIFY(pipeline.lastError().contains("Retained"));
        QVERIFY(!pipeline.snapshot("RobotStaticStatus"));
        rm_server_to_client::RobotStaticStatus incoming;
        QCOMPARE(pipeline.publish(incoming), quint64(0));
        QVERIFY(broker.received_publications.isEmpty());
        QVERIFY(!pipeline.start(MqttSettings{}));
        QCOMPARE(pipeline.connectionState(), QString("ready"));
    }

    void qosOneAckRateLimitAndNoReplayAfterTimeout() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "3";
        settings.is_official = true;
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        rm_client_to_server::CommonCommand command;
        command.set_cmd_type(1);
        command.set_param(10);
        QSignalSpy results(&pipeline, &DataPipeline::publishFinished);
        QVERIFY(pipeline.publish(command, 1000) != 0);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.at(0).at(2).toString(), QString("acknowledged"));
        // 两次同时排队必有一项被限频；PUBACK 仅表示 Broker 确认，不是执行确认。
        QTest::qWait(110);
        results.clear();
        QVERIFY(pipeline.publish(command, 1000) != 0);
        QVERIFY(pipeline.publish(command, 1000) != 0);
        QTRY_COMPARE(results.size(), 2);
        QStringList outcomes;
        for (const auto& result : results) outcomes.append(result.at(2).toString());
        QVERIFY(outcomes.contains("rate_limited"));
        QVERIFY(outcomes.contains("acknowledged"));
        QTest::qWait(110);
        broker.acknowledge_publication = false;
        results.clear();
        const auto before = broker.received_publications.size();
        QVERIFY(pipeline.publish(command, 200) != 0);
        QTRY_COMPARE(broker.received_publications.size(), before + 1);
        QTRY_COMPARE(pipeline.connectionState(), QString("reconnecting"));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first().at(2).toString(), QString("failed"));
        QCOMPARE(pipeline.publish(command), quint64(0));
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 4000);
        QTest::qWait(150);
        QCOMPARE(broker.received_publications.size(), before + 1);
    }

    void eventOverflowIsBoundedAndReported() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "3";
        settings.is_official = true;
        settings.bindings = {{"Event", "rm_server_to_client.Event"}};
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        // 每次注入后等待解析，稳定覆盖 GUI 消费滞后；不得无限增长或悄悄当作完整事件记录。
        for (int i = 0; i < 135; ++i) {
            broker.publish("Event", QByteArray::fromHex("0801"));
            QTRY_COMPARE(pipeline.diagnostics().value("receivedCount").toInt(), i + 1);
        }
        const auto events = pipeline.takeEvents();
        QCOMPARE(events.size(), 128);
        QCOMPARE(pipeline.diagnostics().value("guiEventOverflow").toInt(), 7);
        broker.publish("Event", QByteArray::fromHex("0801"));
        QTRY_COMPARE(pipeline.diagnostics().value("receivedCount").toInt(), 136);
        QCOMPARE(pipeline.diagnostics().value("guiEventOverflow").toInt(), 7);
        QCOMPARE(pipeline.takeEvents().size(), 1);
    }

    void officialCatalogValidatesPresenceAndLimits() {
        QVERIFY(!protocolRule("RadarInfoToClient"));
        QVERIFY(officialReceiveBindings().contains("GameStatus"));
        rm_client_to_server::CustomControl custom;
        custom.set_data(std::string(60, '\0'));
        QVERIFY(validateProtocolMessage(custom).isEmpty());
        custom.set_data(std::string(61, '\0'));
        QVERIFY(!validateProtocolMessage(custom).isEmpty());
        rm_server_to_client::RobotStaticStatus robot;
        QVERIFY(validateProtocolMessage(robot, 3).isEmpty());
        robot.set_robot_id(4);
        QVERIFY(!validateProtocolMessage(robot, 3).isEmpty());
    }

    void publishOnlySessionUsesSameBoundConnection() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.local_address = QHostAddress::LocalHost;
        settings.port = broker.serverPort();
        settings.client_id = "3";
        settings.is_official = true;
        rm_client_to_server::CustomControl custom;
        custom.set_data(std::string("\0\x80\xff", 3));
        QCOMPARE(pipeline.publish(custom), quint64(0));
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        const auto request = pipeline.publish(custom);
        QVERIFY(request != 0);
        QTRY_COMPARE(broker.received_publications.size(), 1);
        QCOMPARE(broker.received_publications.first().first, QString("CustomControl"));
        rm_client_to_server::CustomControl received;
        QVERIFY(
            received.ParseFromString(broker.received_publications.first().second.toStdString()));
        QCOMPARE(received.data(), custom.data());
        QCOMPARE(broker.connection_count, 1);
        QCOMPARE(pipeline.diagnostics().value("localAddress").toString(), QString("127.0.0.1"));
        rm_client_to_server::KeyboardMouseControl neutral;
        neutral.set_mouse_x(0);
        neutral.set_mouse_y(0);
        neutral.set_mouse_z(0);
        neutral.set_left_button_down(false);
        neutral.set_right_button_down(false);
        neutral.set_mid_button_down(false);
        neutral.set_keyboard_value(0);
        QVERIFY(pipeline.publish(neutral) != 0);
        QTRY_COMPARE(broker.received_publications.size(), 2);
        QCOMPARE(broker.received_publications.last().first, QString("KeyboardMouseControl"));
        QCOMPARE(broker.received_publications.last().second,
                 QByteArray::fromStdString(neutral.SerializeAsString()));
        QCOMPARE(broker.connection_count, 1);
        pipeline.stop();
        QCOMPARE(pipeline.publish(custom), quint64(0));
    }

    void officialEventsAndStreamDoNotCollapseIntoSnapshot() {
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "3";
        settings.robot_id = 3;
        settings.is_official = true;
        settings.bindings = {{"Event", "rm_server_to_client.Event"},
                             {"RobotStaticStatus", "rm_server_to_client.RobotStaticStatus"}};
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        broker.publish("Event", QByteArray::fromHex("0801"));
        QTest::qWait(5);
        broker.publish("Event", QByteArray::fromHex("0802"));
        QList<DataPipeline::ReceivedMessage> events;
        QTRY_VERIFY(([&] {
            events.append(pipeline.takeEvents());
            return events.size() == 2;
        })());
        QVERIFY(!pipeline.snapshot("Event"));
        broker.publish("RobotStaticStatus", QByteArray::fromHex("2004"));
        QTRY_VERIFY(pipeline.lastError().contains("identity"));
        QVERIFY(!pipeline.snapshot("RobotStaticStatus"));
        broker.publish("RobotStaticStatus", QByteArray::fromHex("2003"));
        QTRY_VERIFY(pipeline.snapshot("RobotStaticStatus"));
        QVERIFY(pipeline.receivedAt("RobotStaticStatus") >= 0);
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
        // 只在测试中绑定消息通知计数，产品 QML 不映射比赛业务字段。
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
