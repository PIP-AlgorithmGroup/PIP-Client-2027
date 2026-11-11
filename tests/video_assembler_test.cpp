#include "video/video_assembler.h"

#include <QTest>
#include <QtEndian>

namespace {
// 模拟官方 8 字节大端头；测试不依赖真实设备或系统墙钟。
QByteArray datagram(quint16 frame, quint16 index, quint32 size, const QByteArray& payload) {
    QByteArray packet(8, '\0');
    qToBigEndian(frame, packet.data());
    qToBigEndian(index, packet.data() + 2);
    qToBigEndian(size, packet.data() + 4);
    return packet + payload;
}
}  // namespace

/** 官方 UDP 分片边界与工程资源策略的确定性回归。 */
class VideoAssemblerTest : public QObject {
    Q_OBJECT
   private slots:
    void reorderedDuplicateAndWrap() {
        VideoAssembler assembler(0);
        const QByteArray first(1392, 'a');
        QVERIFY(assembler.push(datagram(65535, 1, 1395, "end"), 0).isEmpty());
        QVERIFY(assembler.push(datagram(65535, 1, 1395, "end"), 1).isEmpty());
        QCOMPARE(assembler.push(datagram(65535, 0, 1395, first), 2), first + "end");
        QVERIFY(assembler.push(datagram(65535, 0, 1395, first), 3).isEmpty());
        QCOMPARE(assembler.push(datagram(0, 0, 3, "new"), 4), QByteArray("new"));
        QVERIFY(assembler.push(datagram(65534, 0, 3, "old"), 5).isEmpty());
    }
    void oneBased() {
        VideoAssembler assembler(1);
        QCOMPARE(assembler.push(datagram(1, 1, 3, "one"), 0), QByteArray("one"));
        QVERIFY(assembler.push(datagram(2, 0, 3, "bad"), 1).isEmpty());
    }
    void repeatedWrapDoesNotAccumulateStaleFrameIds() {
        VideoAssembler assembler(0);
        for (quint32 sequence = 0; sequence < 131074; ++sequence) {
            QCOMPARE(
                assembler.push(datagram(static_cast<quint16>(sequence), 0, 3, "new"), sequence),
                QByteArray("new"));
        }
        QCOMPARE(assembler.pendingCount(), 0);
    }
    void rejectsMalformedAndConflicting() {
        VideoAssembler assembler(0);
        for (const auto& packet :
             {QByteArray(7, 'x'), datagram(1, 0, 0, ""), datagram(1, 0, 2097153, "x"),
              datagram(1, 0, 2, "bad"), datagram(1, 3, 3, "bad")}) {
            QVERIFY(assembler.push(packet, 0).isEmpty());
        }
        QVERIFY(assembler.push(datagram(2, 0, 1395, QByteArray(1392, 'a')), 0).isEmpty());
        QVERIFY(assembler.push(datagram(2, 0, 1395, QByteArray(1392, 'b')), 1).isEmpty());
        QVERIFY(assembler.push(datagram(2, 1, 1395, "end"), 2).isEmpty());
    }
    void variableLengthParts() {
        VideoAssembler assembler(0);
        QVERIFY(assembler.push(datagram(1, 1, 6, "def"), 0).isEmpty());
        QCOMPARE(assembler.push(datagram(1, 0, 6, "abc"), 1), QByteArray("abcdef"));
        QVERIFY(assembler.push(datagram(2, 0, 6, "abc"), 2).isEmpty());
        QVERIFY(assembler.push(datagram(2, 2, 6, "def"), 3).isEmpty());
    }
    void expiredFrameCannotComplete() {
        VideoAssembler assembler(0);
        QVERIFY(assembler.push(datagram(1, 0, 1395, QByteArray(1392, 'a')), 0).isEmpty());
        assembler.expire(151);
        QVERIFY(assembler.push(datagram(1, 1, 1395, "end"), 152).isEmpty());
        QCOMPARE(assembler.push(datagram(2, 0, 3, "new"), 153), QByteArray("new"));
    }
    void inflightFramesAreBounded() {
        VideoAssembler assembler(0);
        for (quint16 frame = 0; frame < 100; ++frame) {
            assembler.push(datagram(frame, 0, 2097152, QByteArray(1392, 'a')), frame);
        }
        QVERIFY(assembler.pendingCount() <= 4);
    }
};
QTEST_GUILESS_MAIN(VideoAssemblerTest)
#include "video_assembler_test.moc"
