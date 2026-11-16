#include "video/video_receiver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <thread>

#include "support/video_test_packets.h"

/** 真实回环 UDP → 重组 → FFmpeg → GUI 单槽邮箱，不依赖比赛设备。 */
class VideoReceiverTest : public QObject {
    Q_OBJECT
   private slots:
    void recoveryEventsExplainFrameGapAndDecoderReset() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0, "cpu"));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1));
        QTRY_VERIFY(!receiver.frame().isNull());
        QVERIFY(video_test::sendFrame(sender, port, 3));
        // 最新帧编号保证等到重置后的真实输出，而非误读重置前留在 GUI 的图像。
        QTRY_COMPARE(receiver.frame().frame_id, 3);
        const auto frame = receiver.frame();
        QVERIFY(frame.gui_received_us >= frame.decoded_us);
        const auto batch = receiver.takeRecoveryEvents();
        bool saw_queue = false;
        bool saw_decoder = false;
        for (const auto& value : batch.value("events").toList()) {
            const auto event = value.toMap();
            if (event.value("reason").toString() != "frame_gap") continue;
            QCOMPARE(event.value("frameId").toInt(), 3);
            QCOMPARE(event.value("previousFrameId").toInt(), 1);
            QCOMPARE(event.value("lastDecodeFrameId").toInt(), 1);
            QCOMPARE(event.value("lastDecodeEpoch").toULongLong(), 0ULL);
            QVERIFY(event.value("lastDecodeTotalMs", -1).toDouble() >= 0);
            QVERIFY(event.value("lastDecodeCodecMs", -1).toDouble() >= 0);
            QCOMPARE(event.value("lastDecodeDownloadMs").toDouble(), -1.0);
            QCOMPARE(event.value("lastDecodeConvertMs", -1).toDouble(), -1.0);
            QVERIFY(event.value("evidenceUs").toLongLong() > 0);
            QVERIFY(event.value("atUs").toLongLong() > 0);
            if (event.value("event").toString() == "queue_reset") saw_queue = true;
            if (event.value("event").toString() == "decoder_reset") {
                saw_decoder = true;
                QVERIFY(event.value("durationMs").toDouble() >= 0);
            }
        }
        QVERIFY(saw_queue);
        QVERIFY(saw_decoder);
        QVERIFY(receiver.takeRecoveryEvents().value("events").toList().isEmpty());
        // 不消费诊断时也必须有界，且超额必须可见，不能静默丢掉重置原因。
        for (int id = 5; id <= 43; id += 2) {
            QVERIFY(video_test::sendFrame(sender, port, id));
            QTRY_COMPARE(receiver.frame().frame_id, id);
        }
        const auto overflow = receiver.takeRecoveryEvents();
        QCOMPARE(overflow.value("events").toList().size(), 32);
        QCOMPARE(overflow.value("omitted").toULongLong(), 8ULL);
        receiver.stop();
        const auto stopped = receiver.takeRecoveryEvents();
        QCOMPARE(stopped.value("events").toList().last().toMap().value("reason").toString(),
                 QString("session_stop"));
        QCOMPARE(stopped.value("omitted").toULongLong(), 0ULL);
    }
    void decoderChoicePersistsAndRejectsInvalidConfiguration() {
        QTemporaryDir directory;
        const auto path = directory.filePath("video.ini");
        VideoReceiver receiver(path);
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0, "cpu"));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(!receiver.configure("127.0.0.1", port, 0, "invalid\nport=7"));
        QVERIFY(!receiver.configure("127.0.0.1", port, 0, "cpu\n"));
        QVERIFY(receiver.running());
        QCOMPARE(receiver.decoderChoice(), QString("cpu"));
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        QCOMPARE(receiver.decoderChoice(), QString("cpu"));
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1));
        QTRY_VERIFY(!receiver.frame().isNull());
        QTRY_COMPARE(receiver.diagnostics().value("decoder").toString(), QString("CPU"));
        QTRY_VERIFY(receiver.diagnostics()
                        .value("timings")
                        .toMap()
                        .value("codec")
                        .toMap()
                        .value("samples")
                        .toInt() > 0);
        receiver.stop();
        VideoReceiver restored(path);
        QCOMPARE(restored.decoderChoice(), QString("cpu"));
        QTRY_VERIFY_WITH_TIMEOUT(!restored.probingDevices(), 15000);
        QVERIFY(restored.decoderDevices().size() >= 2);
    }
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
        while (receiver.frame().isNull() && elapsed.elapsed() < 1500) {
            QTest::qSleep(10);
            QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        }
        QVERIFY2(!receiver.frame().isNull(), "已解码帧应主动唤醒 GUI，无需定时轮询");
        receiver.stop();
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QCOMPARE(receiver.status(), QString("已停止"));
        QVERIFY(receiver.frame().isNull());
    }
    void blockedGuiCoalescesFramesAndRejectsStoppedSessionWake() {
        QTemporaryDir directory;
        VideoReceiver receiver(directory.filePath("video.ini"));
        const auto port = video_test::freePort();
        QVERIFY(receiver.configure("127.0.0.1", port, 0));
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QUdpSocket sender;
        // socket 就绪不代表硬件上下文已经初始化；先取得真实输出，再测试 GUI 邮箱合并。
        QVERIFY(video_test::sendFrame(sender, port, 0));
        QTRY_VERIFY(!receiver.frame().isNull());
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QSignalSpy frames(&receiver, &VideoReceiver::frameChanged);
        // 暂停 GUI 消费，允许多帧覆盖邮箱；首次恢复只消费至多一条已合并的唤醒。
        for (quint16 id = 1; id < 13; ++id) {
            QVERIFY(video_test::sendFrame(sender, port, id));
            QTest::qSleep(10);
        }
        QFile hd(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(hd.open(QIODevice::ReadOnly));
        const auto last_unit = hd.readAll();
        // 最后一张改用不同尺寸，验证拿到的是最新帧，而非仅验证通知数量。
        for (int offset = 0, index = 0; offset < last_unit.size(); offset += 1392, ++index) {
            QByteArray packet(8, '\0');
            qToBigEndian(quint16(13), packet.data());
            qToBigEndian(static_cast<quint16>(index), packet.data() + 2);
            qToBigEndian(static_cast<quint32>(last_unit.size()), packet.data() + 4);
            packet.append(last_unit.mid(offset, 1392));
            QVERIFY(sender.writeDatagram(packet, QHostAddress::LocalHost, port) > 0);
        }
        QTest::qSleep(200);
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        // 200ms 只模拟 GUI 阻塞，不能当作解码完成屏障；合法清屏也可能合并到这一通知。
        QVERIFY(frames.count() <= 1);
        QTRY_COMPARE(receiver.frame().size(), QSize(1920, 1080));
        QVERIFY(video_test::sendFrame(sender, port, 14));
        QTest::qSleep(100);
        receiver.stop();
        receiver.start();
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QVERIFY(receiver.frame().isNull());
        QVERIFY(receiver.running());
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(video_test::sendFrame(sender, port, 0));
        QTRY_VERIFY(!receiver.frame().isNull());
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
        QTRY_VERIFY(!receiver.frame().isNull());
        QTRY_VERIFY_WITH_TIMEOUT(receiver.diagnostics().value("decodeMs", -1).toDouble() >= 0,
                                 1000);
        QVERIFY(!receiver.diagnostics().value("decoder").toString().isEmpty());
        QVERIFY(receiver.diagnostics().contains("hardwareActive"));
        QVERIFY(receiver.diagnostics().value("decodeMaxMs").toDouble() >=
                receiver.diagnostics().value("decodeMs").toDouble());
        QTRY_VERIFY(receiver.diagnostics().value("decodeQueueMs", -1).toDouble() >= 0);
        QVERIFY(receiver.diagnostics().contains("decodeQueueDropped"));
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
            QTRY_VERIFY(!receiver.frame().isNull());
            QElapsedTimer elapsed;
            elapsed.start();
            QVERIFY(receiver.configure("127.0.0.1", port, 0));
            receiver.stop();
            QVERIFY2(elapsed.elapsed() < 1000, "保存/停止不能长时间阻塞 GUI 线程");
            QVERIFY(!receiver.running());
            QVERIFY(receiver.frame().isNull());
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
        QVERIFY(receiver.frame().isNull());
        QVERIFY(video_test::sendFrame(foreign, port, 2));
        QTest::qWait(40);
        QCOMPARE(receiver.property("sourcePort").toInt(), int(first.localPort()));
        QTRY_VERIFY_WITH_TIMEOUT(receiver.property("sourceAddress").toString().isEmpty(), 2500);
        QCOMPARE(receiver.property("sourcePort").toInt(), 0);
        QVERIFY(video_test::sendFrame(foreign, port, 3));
        QTRY_COMPARE(receiver.property("sourcePort").toInt(), int(foreign.localPort()));
        QTRY_VERIFY(!receiver.frame().isNull());
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
        QTRY_COMPARE(receiver.frame().size(), QSize(320, 180));
        QCOMPARE(receiver.frame().pixel_aspect, 1.0);
        QVERIFY(receiver.error().isEmpty());
        QVERIFY(video_test::sendFrame(sender, port, 0, base));
        QTRY_COMPARE(receiver.frame().frame_id, 0);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.frame().isNull(), 2500);
        // 清屏也可能先由输出超时触发；来源解锁后再检查对应的队列清空原因。
        QTRY_VERIFY_WITH_TIMEOUT(receiver.sourceAddress().isEmpty(), 2500);
        bool saw_timeout = false;
        for (const auto& value : receiver.takeRecoveryEvents().value("events").toList()) {
            const auto event = value.toMap();
            if (event.value("event") != "queue_reset" || event.value("reason") != "source_timeout")
                continue;
            saw_timeout = true;
            QCOMPARE(event.value("frameId").toInt(), 0);
        }
        QVERIFY(saw_timeout);
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
        QVERIFY(receiver.frame().isNull());
        QVERIFY(video_test::sendFrame(sender, port, 2));
        QTRY_VERIFY(!receiver.frame().isNull());
        receiver.stop();
        QVERIFY(receiver.frame().isNull());
        receiver.start();
        QTRY_COMPARE(receiver.status(), QString("等待图传"));
        QVERIFY(video_test::sendFrame(sender, port, 0));
        QTRY_VERIFY(!receiver.frame().isNull());
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
        QVERIFY(receiver.frame().isNull());
        QVERIFY(video_test::sendFrame(first, port, 2));
        QTRY_VERIFY(!receiver.frame().isNull());
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
        QTRY_VERIFY(!receiver.frame().isNull());
        QVERIFY(receiver.error().contains("保存"));
    }
};
QTEST_GUILESS_MAIN(VideoReceiverTest)
#include "video_receiver_test.moc"
