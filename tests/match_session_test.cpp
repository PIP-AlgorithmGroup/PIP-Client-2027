#include "app/match_session.h"

#include <QFile>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QTemporaryDir>
#include <QTest>

#include "client_to_server.pb.h"
#include "mqtt_test_broker.h"
#include "server_to_client.pb.h"
#include "video_test_packets.h"

namespace {
// 通过 Qt 枚举实际回环接口名，两平台不依赖 lo/Loopback 等平台专有名称。
QString loopbackInterface() {
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        for (const auto& entry : interface.addressEntries()) {
            if (entry.ip() == QHostAddress::LocalHost) return interface.name();
        }
    }
    return {};
}
}  // namespace

/** 本机 TCP MQTT 与真实 UDP/HEVC 组合验证；不模拟官方多席位路由或物理 RJ45。 */
class MatchSessionTest : public QObject {
    Q_OBJECT
   private slots:
    void profilesAreIndependentAndOfficialEndpointsAreImmutable() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto iface = loopbackInterface();
        QVERIFY(!iface.isEmpty());
        DataPipeline pipeline;
        VideoReceiver video(dir.filePath("video.ini"));
        MatchSession session(pipeline, video, dir.filePath("session.json"));
        QVERIFY(!session.active());
        QVERIFY(!session.configure(iface, "127.0.0.1", 3, "127.0.0.1", 1883));
        QCOMPARE(session.configuration().value("brokerHost").toString(), QString("192.168.12.1"));
        QVERIFY(session.selectProfile("development"));
        QVERIFY(session.configure(iface, "127.0.0.1", 3, "127.0.0.1", 1883));
        QVERIFY(session.networkReady());
        QVERIFY(!session.start());  // 欢迎页不能提前建立比赛会话。
        QVERIFY(session.selectProfile("official"));
        QCOMPARE(session.configuration().value("brokerPort").toInt(), 3333);
        QCOMPARE(session.configuration().value("robotId").toInt(), 0);
        QVERIFY(session.selectProfile("development"));
        QCOMPARE(session.configuration().value("brokerPort").toInt(), 1883);
        QCOMPARE(session.configuration().value("clientId").toString(), QString("3"));
        QVERIFY(!session.configure(iface, "127.0.0.1", 2, "127.0.0.1", 1883));
        QCOMPARE(session.configuration().value("robotId").toInt(), 3);
    }

    void corruptConfigurationIsNotPartiallyApplied() {
        QTemporaryDir dir;
        QFile file(dir.filePath("session.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(
            file.write(
                R"({"version":1,"official":{"interfaceName":"bad","localAddress":"127.0.0.1","robotId":3}})") >
            0);
        file.close();
        DataPipeline pipeline;
        VideoReceiver video(dir.filePath("video.ini"));
        MatchSession session(pipeline, video, file.fileName());
        QCOMPARE(session.configuration().value("localAddress").toString(), QString("192.168.12.2"));
        QCOMPARE(session.configuration().value("robotId").toInt(), 0);
        QVERIFY(!session.error().isEmpty());
        QVERIFY(!session.active());
        QVERIFY(!session.selectProfile("official"));
    }

    void mqttAndVideoRecoverIndependentlyAndReleaseResources() {
        QTemporaryDir dir;
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        VideoReceiver video(dir.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(port != 0);
        QVERIFY(video.configure("127.0.0.1", port, 0));
        MatchSession session(pipeline, video, dir.filePath("session.json"));
        QVERIFY(session.selectProfile("development"));
        QVERIFY(session.configure(loopbackInterface(), "127.0.0.1", 3, "127.0.0.1",
                                  broker.serverPort()));
        session.setArena(true, "RMUL");
        QVERIFY(session.start());
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QTRY_COMPARE(video.status(), QString("等待图传"));
        QCOMPARE(video.bindAddress(), QString("127.0.0.1"));
        QCOMPARE(pipeline.diagnostics().value("localAddress").toString(), QString("127.0.0.1"));
        // 五类最小状态走同一连接；缺省字段仍保持缺省，不把 0/false 当作缺失。
        rm_server_to_client::GameStatus game;
        game.set_current_round(1);
        game.set_current_stage(4);
        game.set_game_result(255);
        game.set_end_reason(255);
        game.set_is_paused(false);
        rm_server_to_client::GlobalUnitStatus units;
        for (int i = 0; i < 8; ++i) units.add_robot_health(100);
        rm_server_to_client::RobotDynamicStatus dynamic;
        rm_server_to_client::RobotModuleStatus modules;
        const google::protobuf::Message* samples[] = {&game, &units, &dynamic, &modules};
        for (const auto* message : samples) {
            const auto topic = QString::fromStdString(message->GetDescriptor()->name());
            broker.publish(topic, QByteArray::fromStdString(message->SerializeAsString()));
            QTRY_VERIFY(pipeline.snapshot(topic));
        }
        const auto game_snapshot = std::dynamic_pointer_cast<const rm_server_to_client::GameStatus>(
            pipeline.snapshot("GameStatus"));
        QVERIFY(game_snapshot->has_is_paused());
        QVERIFY(!game_snapshot->is_paused());
        QVERIFY(!game_snapshot->has_stage_elapsed_sec());
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1, 0, true));
        QTRY_VERIFY(!video.frame().image.isNull());
        broker.publish("RobotStaticStatus", QByteArray::fromHex("2003"));
        QTRY_VERIFY(pipeline.snapshot("RobotStaticStatus"));
        broker.dropConnection();
        QTRY_COMPARE(pipeline.connectionState(), QString("reconnecting"));
        QVERIFY(!pipeline.snapshot("RobotStaticStatus"));
        QVERIFY(video.running());
        QVERIFY(video_test::sendFrame(sender, port, 2));
        QTRY_VERIFY(!video.frame().image.isNull());
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 4000);
        QVERIFY(!pipeline.snapshot("RobotStaticStatus"));
        QCOMPARE(broker.connection_count, 2);
        QVERIFY(broker.received_publications.isEmpty());
        session.stopVideo();
        QVERIFY(!video.running());
        QCOMPARE(pipeline.connectionState(), QString("ready"));
        broker.publish("RobotStaticStatus", QByteArray::fromHex("2003"));
        QTRY_VERIFY(pipeline.snapshot("RobotStaticStatus"));
        // 非法修改不能清除活动会话；合法身份变更必须清除旧状态，不自动恢复用户暂停的视频。
        QVERIFY(!session.configure(loopbackInterface(), "127.0.0.2", 4, "127.0.0.1",
                                   broker.serverPort()));
        QCOMPARE(pipeline.connectionState(), QString("ready"));
        QVERIFY(session.configure(loopbackInterface(), "127.0.0.1", 4, "127.0.0.1",
                                  broker.serverPort()));
        QVERIFY(!pipeline.snapshot("RobotStaticStatus"));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QVERIFY(!video.running());
        session.startVideo();
        QTRY_COMPARE(video.status(), QString("等待图传"));
        session.stop();
        QVERIFY(video.running());
        QCOMPARE(pipeline.connectionState(), QString("offline"));
        QVERIFY(video_test::sendFrame(sender, port, 3));
        QTRY_VERIFY(!video.frame().image.isNull());
        session.setArena(false, "RMUL");
        QVERIFY(!video.running());
        QVERIFY(video.frame().image.isNull());
        QVERIFY(!session.active());
        QUdpSocket probe;
        QVERIFY(probe.bind(QHostAddress::LocalHost, port, QUdpSocket::DontShareAddress));
    }

    void saveFailureKeepsRunningSession() {
        QTemporaryDir dir;
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        VideoReceiver video(dir.filePath("video.ini"));
        const auto path = dir.filePath("session.json");
        MatchSession session(pipeline, video, path);
        QVERIFY(session.selectProfile("development"));
        QVERIFY(session.configure(loopbackInterface(), "127.0.0.1", 3, "127.0.0.1",
                                  broker.serverPort()));
        session.setArena(true, "RMUC");
        QVERIFY(session.start());
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QFile damaged(path);
        QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(damaged.write("broken"), qint64(6));
        damaged.close();
        QVERIFY(!session.configure(loopbackInterface(), "127.0.0.1", 4, "127.0.0.1",
                                   broker.serverPort()));
        QCOMPARE(session.configuration().value("robotId").toInt(), 3);
        QCOMPARE(pipeline.connectionState(), QString("ready"));
        QVERIFY(session.active());
    }
};

QTEST_GUILESS_MAIN(MatchSessionTest)
#include "match_session_test.moc"
