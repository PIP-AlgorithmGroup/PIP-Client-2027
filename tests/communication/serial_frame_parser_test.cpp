#include "communication/serial_frame_parser.h"

#include <QTest>

/** 固定向量来自独立的逐位 CRC 计算；不使用产品编码器生成期望值。 */
class SerialFrameParserTest : public QObject {
    Q_OBJECT

   private slots:
    void fragmentedBinaryAndSequenceWrap() {
        SerialFrameParser parser;
        const auto frame = QByteArray::fromHex("a50300ff12110300a5ffcd58");
        QVERIFY(parser.feed(frame.first(4), 0).isEmpty());
        QCOMPARE(parser.feed(frame.sliced(4), 1), QList<QByteArray>{QByteArray::fromHex("00a5ff")});
        const auto maximum = QByteArray::fromHex(
            "a53c0000a61103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
            "202122232425262728292a2b2c2d2e2f303132333435363738393a3bb21e");
        QCOMPARE(parser.feed(maximum, 2).size(), 1);
        QCOMPARE(parser.acceptedCount(), quint64(2));
        const auto empty = QByteArray::fromHex("a50000019d1103ddb2");
        QCOMPARE(parser.feed(empty, 3), QList<QByteArray>{QByteArray{}});
        QVERIFY(parser.feed(maximum, 4).isEmpty());  // seq=0 位于最近 seq=1 之前。
        QCOMPARE(parser.discardedSequenceCount(), quint64(1));
    }

    void noiseCorruptFrameAndConcatenatedFramesRecover() {
        SerialFrameParser parser;
        auto corrupt = QByteArray::fromHex("a50300ff12110300a5ffcd58");
        corrupt[7] = char(1);
        const auto good = QByteArray::fromHex("a50300ff12110300a5ffcd58");
        QCOMPARE(parser.feed(QByteArray(4096, 'x') + corrupt + good, 0).size(), 1);
        QVERIFY(parser.errorCount() > 0);
        QCOMPARE(parser.feed(good + good, 1).size(), 0);
        QCOMPARE(parser.discardedSequenceCount(), quint64(2));
    }

    void headerLengthAndCommandAreValidated() {
        SerialFrameParser parser;
        const auto good = QByteArray::fromHex("a50300ff12110300a5ffcd58");
        auto bad_header = good;
        bad_header[4] ^= 1;
        auto bad_length = good;
        bad_length[1] = char(61);
        auto bad_command = good;
        bad_command[5] = char(0x10);
        QVERIFY(parser.feed(bad_header + bad_length + bad_command, 0).isEmpty());
        QCOMPARE(parser.feed(good, 1).size(), 1);
    }

    void validUnsupportedFrameDoesNotExposeNestedPayloadAsACommand() {
        SerialFrameParser parser;
        // 外层 CRC 均正确，但命令为 0x0310；其负载恰好是一份完整 0x0311 帧。
        const auto unsupported = QByteArray::fromHex("a50c0001261003a50300ff12110300a5ffcd582e70");
        QVERIFY(parser.feed(unsupported, 0).isEmpty());
        QCOMPARE(parser.acceptedCount(), quint64(0));
    }

    void partialFrameExpiresAndSequenceResetsAfterSilence() {
        SerialFrameParser parser;
        const auto good = QByteArray::fromHex("a50300ff12110300a5ffcd58");
        QVERIFY(parser.feed(good.first(8), 0).isEmpty());
        QVERIFY(parser.feed({}, 100).isEmpty());
        QVERIFY(parser.feed(good.sliced(8), 101).isEmpty());
        QCOMPARE(parser.feed(good, 102).size(), 1);
        QVERIFY(parser.feed(good, 103).isEmpty());
        QCOMPARE(parser.feed(good, 203).size(), 1);
        QVERIFY(parser.feed(good.first(8), 204).isEmpty());
        parser.resetStream();
        QVERIFY(parser.feed(good.sliced(8), 205).isEmpty());
        QCOMPARE(parser.acceptedCount(), quint64(2));
        parser.reset();
        QCOMPARE(parser.acceptedCount(), quint64(0));
        QCOMPARE(parser.feed(good, 204).size(), 1);
    }
};

QTEST_GUILESS_MAIN(SerialFrameParserTest)
#include "serial_frame_parser_test.moc"
