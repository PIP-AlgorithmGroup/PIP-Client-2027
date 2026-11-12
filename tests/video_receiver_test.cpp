#include "video/video_receiver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <thread>

#include "video_test_packets.h"

/** 真实回环 UDP → 重组 → FFmpeg → GUI 单槽邮箱，不依赖比赛设备。 */
class VideoReceiverTest : public QObject {
    Q_OBJECT
   private slots:
    void decodedFrameWakesGuiWithoutPollingTimer() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1));
        // 不运行 GUI 定时器；只投递队列调用。旧的 16ms 轮询不能通过此入口发布帧。
        QElapsedTimer elapsed;
        elapsed.start();
        while (receiver.frame().image.isNull() && elapsed.elapsed() < 1500) {
            QTest::qSleep(10);
            QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        }
        QVERIFY2(!receiver.frame().image.isNull(), "已解码帧应主动唤醒 GUI，无需定时轮询");
        receiver.stop();
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QCOMPARE(receiver.status(), QString("已停止"));
        QVERIFY(receiver.frame().image.isNull());
    }
    void blockedGuiCoalescesFramesAndRejectsStoppedSessionWake() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QSignalSpy frames(&receiver, &VideoReceiver::frameChanged);
        // 暂停 GUI 消费，允许多帧覆盖邮箱。恢复时只产生一次最新画面的通知。
        for (quint16 id = 0; id < 12; ++id) {
            QVERIFY(video_test::sendFrame(sender, port, id));
            QTest::qSleep(10);
        }
        QFile hd(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(hd.open(QIODevice::ReadOnly));
        const auto last_unit = hd.readAll();
        // 最后一张改用不同尺寸，验证拿到的是最新帧，而非仅验证通知数量。
        for (int offset = 0, index = 0; offset < last_unit.size(); offset += 1392, ++index) {
            QByteArray packet(8, '\0');
            qToBigEndian(quint16(12), packet.data());
            qToBigEndian(static_cast<quint16>(index), packet.data() + 2);
            qToBigEndian(static_cast<quint32>(last_unit.size()), packet.data() + 4);
            packet.append(last_unit.mid(offset, 1392));
            QVERIFY(sender.writeDatagram(packet, QHostAddress::LocalHost, port) > 0);
        }
        QTest::qSleep(200);
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QCOMPARE(frames.count(), 1);
        QCOMPARE(receiver.frame().image.size(), QSize(1920, 1080));
        QVERIFY(video_test::sendFrame(sender, port, 13));
        QTest::qSleep(100);
        receiver.stop();
        receiver.start();
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QVERIFY(receiver.frame().image.isNull());
        QVERIFY(receiver.running());
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(video_test::sendFrame(sender, port, 0));
        QTRY_VERIFY(!receiver.frame().image.isNull());
    }
    void diagnosticsReportLocalReceptionAndResetOnStop() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1));
        QTRY_VERIFY(!receiver.frame().image.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(receiver.diagnostics().value("decodeMs", -1).toDouble() >= 0,
                                 1000);
        QVERIFY(!receiver.diagnostics().value("decoder").toString().isEmpty());
        QVERIFY(receiver.diagnostics().contains("hardwareActive"));
        QCOMPARE(receiver.diagnostics().value("packetLossPercent").toDouble(), -1.0);
        // 统计清空与停止同代完成，不能把上次来源的速率带到下一次会话。
        receiver.stop();
        QVERIFY(receiver.diagnostics().isEmpty());
    }
    void repeatedConfigureAndStopUnderVideoLoad_data() {
        QTest::addColumn<QString>("fixture_path");
        QTest::newRow("small") << QString::fromUtf8(PIP_VIDEO_FIXTURE);
        QTest::newRow("1080p") << QString::fromUtf8(PIP_VIDEO_HD_FIXTURE);
    }
    // 与故障入口相同：持续有数据时保存配置会 stop/join，再重启；不能只测空闲停止。
    void repeatedConfigureAndStopUnderVideoLoad() {
        QFETCH(QString, fixture_path);
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QFile fixture(fixture_path);
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto unit = fixture.readAll();
        QVERIFY(!unit.isEmpty());
        // sender 独占 socket；RAII 先请求停止并 join，断言提前返回也不遗留线程。
        std::jthread sender([&](std::stop_token stop) {
            QUdpSocket socket;
            quint16 id = 0;
            while (!stop.stop_requested()) {
                for (int offset = 0, index = 0; offset < unit.size(); offset += 1392, ++index) {
                    QByteArray packet(8, '\0');
                    qToBigEndian(id, packet.data());
                    qToBigEndian(static_cast<quint16>(index), packet.data() + 2);
                    qToBigEndian(static_cast<quint32>(unit.size()), packet.data() + 4);
                    packet.append(unit.mid(offset, 1392));
                    socket.writeDatagram(packet, QHostAddress::LocalHost, port);
                }
                ++id;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });
        for (int cycle = 0; cycle < 20; ++cycle) {
            QTRY_VERIFY(!receiver.frame().image.isNull());
            QElapsedTimer elapsed;
            elapsed.start();
            QVERIFY(receiver.configure("127.0.0.1", port, 0));
            receiver.stop();
            QVERIFY2(elapsed.elapsed() < 1000, "保存/停止不能长时间阻塞 GUI 线程");
            QVERIFY(!receiver.running());
            QVERIFY(receiver.frame().image.isNull());
            receiver.start();
        }
        sender.request_stop();
        sender.join();
        receiver.stop();
    }

    void sourceEndpointTracksLockedSession() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        // QML 只能读取实际来源；不能用来源字段悄悄修改接收过滤或保存配置。
        const auto* meta = receiver.metaObject();
        for (const auto* name : {"sourceAddress", "sourcePort"}) {
            QVERIFY(meta->indexOfProperty(name) >= 0);
            QVERIFY(!meta->property(meta->indexOfProperty(name)).isWritable());
        }
        QVERIFY(receiver.property("sourceAddress").toString().isEmpty());
        QCOMPARE(receiver.property("sourcePort").toInt(), 0);
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket first;
        QUdpSocket foreign;
        QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(foreign.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(first.writeDatagram("invalid", QHostAddress::LocalHost, port) > 0);
        QTest::qWait(40);
        QVERIFY(receiver.property("sourceAddress").toString().isEmpty());
        // 来源在组帧阶段就可识别，不要求已成功解码；不完整帧仍不能显示。
        QVERIFY(video_test::sendFrame(first, port, 1, 0, false, 1));
        QTRY_COMPARE(receiver.property("sourceAddress").toString(), QString("127.0.0.1"));
        QCOMPARE(receiver.property("sourcePort").toInt(), int(first.localPort()));
        QVERIFY(receiver.frame().image.isNull());
        QVERIFY(video_test::sendFrame(foreign, port, 2));
        QTest::qWait(40);
        QCOMPARE(receiver.property("sourcePort").toInt(), int(first.localPort()));
        QTRY_VERIFY_WITH_TIMEOUT(receiver.property("sourceAddress").toString().isEmpty(), 2500);
        QCOMPARE(receiver.property("sourcePort").toInt(), 0);
        QVERIFY(video_test::sendFrame(foreign, port, 3));
        QTRY_COMPARE(receiver.property("sourcePort").toInt(), int(foreign.localPort()));
        QTRY_VERIFY(!receiver.frame().image.isNull());
        receiver.stop();
        QVERIFY(receiver.property("sourceAddress").toString().isEmpty());
        QCOMPARE(receiver.property("sourcePort").toInt(), 0);
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(receiver.property("sourceAddress").toString().isEmpty());
    }

    void receivesReorderedHevcAndClearsOnTimeout_data() {
        QTest::addColumn<int>("base");
        QTest::newRow("zero") << 0;
        QTest::newRow("one") << 1;
    }
    void receivesReorderedHevcAndClearsOnTimeout() {
        QFETCH(int, base);
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(port != 0);
        QVERIFY(receiver.configure("127.0.0.1", port, base));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 65535, base, true));
        QTRY_COMPARE(receiver.frame().image.size(), QSize(320, 180));
        QCOMPARE(receiver.frame().pixel_aspect, 1.0);
        QVERIFY(receiver.error().isEmpty());
        QVERIFY(video_test::sendFrame(sender, port, 0, base));
        QTRY_COMPARE(receiver.status(), QString("正在接收"));
        QTRY_VERIFY_WITH_TIMEOUT(receiver.frame().image.isNull(), 2500);
        QVERIFY(receiver.running());
        receiver.stop();
        QVERIFY(!receiver.running());
        QUdpSocket probe;
        QVERIFY(probe.bind(QHostAddress::LocalHost, port, QUdpSocket::DontShareAddress));
    }
    void lostFrameIsNotDisplayedAndNextKeyFrameRecovers() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1, 0, false, 1));
        QTest::qWait(200);
        QVERIFY(receiver.frame().image.isNull());
        QVERIFY(video_test::sendFrame(sender, port, 2));
        QTRY_VERIFY(!receiver.frame().image.isNull());
        receiver.stop();
        QVERIFY(receiver.frame().image.isNull());
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(video_test::sendFrame(sender, port, 0));
        QTRY_VERIFY(!receiver.frame().image.isNull());
    }
    void foreignSourceCannotMixFrames() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket first;
        QUdpSocket foreign;
        QVERIFY(video_test::sendFrame(first, port, 1, 0, false, 1));
        QTest::qWait(30);
        QVERIFY(video_test::sendFrame(foreign, port, 1));
        QTest::qWait(200);
        QVERIFY(receiver.frame().image.isNull());
        QVERIFY(video_test::sendFrame(first, port, 2));
        QTRY_VERIFY(!receiver.frame().image.isNull());
    }
    void portConflictAndRetry() {
        QTemporaryDir directory;
        QUdpSocket occupied;
        QVERIFY(occupied.bind(QHostAddress(QHostAddress::LocalHost), quint16(0),
                              QUdpSocket::DontShareAddress));
        VideoReceiver receiver(directory.filePath("video.ini"));
        QVERIFY(receiver.configure("127.0.0.1", occupied.localPort(), 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("接收失败"));
        QVERIFY(!receiver.running());
        QVERIFY(!receiver.error().isEmpty());
        occupied.close();
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(receiver.error().isEmpty());
    }
    void persistsWholeConfigurationAndRejectsInvalidInput() {
        QTemporaryDir directory;
        const auto path = directory.filePath("video.ini");
        VideoReceiver receiver(path);
        QVERIFY(receiver.configure("127.0.0.1", 12345, 1));
        QVERIFY(!receiver.configure("localhost", 12346, 0));
        QVERIFY(!receiver.configure("127.0.0.1", 0, 0));
        QVERIFY(!receiver.configure("127.0.0.1", 12346, 2));
        VideoReceiver restored(path);
        QCOMPARE(restored.bindAddress(), QString("127.0.0.1"));
        QCOMPARE(restored.port(), 12345);
        QCOMPARE(restored.indexBase(), 1);
    }
    void invalidFileRequiresExplicitRepair() {
        QTemporaryDir directory;
        const auto path = directory.filePath("video.ini");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\naddress=127.0.0.1\nport=bad\nindex_base=1\n");
        file.close();
        VideoReceiver receiver(path);
        QCOMPARE(receiver.bindAddress(), QString("0.0.0.0"));
        receiver.start();
        QVERIFY(!receiver.running());
        QVERIFY(!receiver.error().isEmpty());
        QVERIFY(receiver.configure("127.0.0.1", video_test::freePort(), 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
    }
    void saveFailurePreservesSession() {
        QTemporaryDir directory;
        const auto path = directory.filePath("video.ini");
        VideoReceiver receiver(path);
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QVERIFY(!receiver.configure("0.0.0.0", 3334, 1));
        QVERIFY(receiver.running());
        QCOMPARE(receiver.port(), int(port));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1));
        QTRY_VERIFY(!receiver.frame().image.isNull());
        QVERIFY(receiver.error().contains("保存"));
    }
};
QTEST_GUILESS_MAIN(VideoReceiverTest)
#include "video_receiver_test.moc"
