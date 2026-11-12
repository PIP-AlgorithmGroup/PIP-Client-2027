#include "video/video_decode_queue.h"

#include <QTest>
#include <latch>
#include <thread>

/** 消费者阻塞不影响生产者；不用 GPU 时序决定回归成败。 */
class VideoDecodeQueueTest : public QObject {
    Q_OBJECT
   private slots:
    void blockedConsumerDoesNotBlockReception() {
        VideoDecodeQueue queue;
        std::latch entered(1);
        std::latch release(1);
        QByteArray consumed;
        QVERIFY(queue.push("first", 0));
        std::jthread consumer([&] {
            const auto first = queue.take();
            if (first) consumed = first->bytes;
            entered.count_down();
            release.wait();
        });
        entered.wait();
        // 消费者模拟停在驱动调用里；以下完成证明收包无需等待它。
        for (int i = 1; i <= 10; ++i) queue.push(QByteArray::number(i), i * 1000);
        release.count_down();
        consumer.join();
        QCOMPARE(consumed, QByteArray("first"));
        QCOMPARE(queue.dropped(), quint64(9));
        const auto latest = queue.take();
        QVERIFY(latest.has_value());
        QCOMPARE(latest->bytes, QByteArray("10"));
        QCOMPARE(latest->epoch, quint64(3));
    }
    void resetsInvalidateInFlightReferenceChain() {
        VideoDecodeQueue queue;
        QVERIFY(queue.push("old", 100));
        const auto old = queue.take();
        QVERIFY(old.has_value());
        const auto epoch = queue.reset();
        QVERIFY(old->epoch != epoch);
        QVERIFY(queue.push("new", 200));
        const auto next = queue.take();
        QVERIFY(next.has_value());
        QCOMPARE(next->epoch, epoch);
        QCOMPARE(next->received_us, qint64(200));
    }
    void stopAndBounds() {
        VideoDecodeQueue queue;
        QVERIFY(!queue.push({}, 0));
        QVERIFY(!queue.push(QByteArray(2 * 1024 * 1024 + 1, 'x'), 0));
        QVERIFY(queue.push("a", 0));
        queue.stop();
        QVERIFY(!queue.take().has_value());
        QVERIFY(!queue.push("b", 0));
        const auto epoch = queue.epoch();
        queue.stop();
        QCOMPARE(queue.epoch(), epoch);
    }
    void stopReleasesIdleConsumer() {
        VideoDecodeQueue queue;
        std::latch entered(1);
        bool received = true;
        std::jthread consumer([&] {
            entered.count_down();
            received = queue.take().has_value();
        });
        entered.wait();
        queue.stop();
        consumer.join();
        QVERIFY(!received);
    }
};
QTEST_GUILESS_MAIN(VideoDecodeQueueTest)
#include "video_decode_queue_test.moc"
