#include "video/video_receiver.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "video_test_packets.h"

/** 真实回环 UDP → 重组 → FFmpeg → GUI 单槽邮箱，不依赖比赛设备。 */
class VideoReceiverTest : public QObject {
    Q_OBJECT
   private slots:
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
