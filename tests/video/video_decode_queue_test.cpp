#include "video/video_decode_queue.h"

#include <QTest>
#include <chrono>
#include <latch>
#include <thread>

/** 消费者阻塞不影响生产者；不用 GPU 时序决定回归成败。 */
class VideoDecodeQueueTest : public QObject {
    Q_OBJECT
   private slots:
    void overflowPreservesBlockedDecodeAndEnqueueCadence() {
        VideoDecodeQueue queue;
        QVERIFY(queue.push("in flight", 1000, -1, 10, 1000, 1));
        const auto unit = queue.take();
        QVERIFY(unit);
        queue.beginOperation(VideoDecodeQueue::Operation::Decode, unit->epoch, unit->frame_id,
                             2000);
        queue.endOperation(10000, {8000, 2000, 3000, 2500});
        queue.beginOperation(VideoDecodeQueue::Operation::Decode, 0, 11, 11000);
        for (int id = 12; id <= 15; ++id)
            QVERIFY(queue.push("queued", 12000 + (id - 12) * 16000, -1, id,
                               13000 + (id - 12) * 16000, id));
        const auto before = queue.resetContext();
        QCOMPARE(before.value("evidenceUs").toLongLong(), 61000LL);
        QCOMPARE(before.value("oldestQueuedFrameId").toInt(), 12);
        QCOMPARE(before.value("oldestQueueWaitMs").toDouble(), 48.0);
        QCOMPARE(before.value("workerOperation").toString(), QString("decode"));
        QCOMPARE(before.value("workerElapsedMs").toDouble(), 50.0);
        QCOMPARE(before.value("workerFrameId").toInt(), 11);
        QCOMPARE(before.value("workerEpoch").toULongLong(), 0ULL);
        QCOMPARE(before.value("lastDecodeFrameId").toInt(), 10);
        QCOMPARE(before.value("lastDecodeAgeMs").toDouble(), 51.0);
        QCOMPARE(before.value("lastDecodeTotalMs").toDouble(), 8.0);
        QCOMPARE(before.value("lastDecodeCodecMs").toDouble(), 2.0);
        QCOMPARE(before.value("lastDecodeDownloadMs").toDouble(), 3.0);
        QCOMPARE(before.value("lastDecodeConvertMs").toDouble(), 2.5);
        QCOMPARE(before.value("recentEnqueueIntervalsMs").toList(),
                 QVariantList({12.0, 16.0, 16.0, 16.0}));
        QCOMPARE(before.value("recentEnqueueBatchIds").toList(),
                 QVariantList({1ULL, 12ULL, 13ULL, 14ULL, 15ULL}));
        // 旧代解码完成、开始重置及消费新帧，均不能覆盖已捕获的溢出前证据。
        queue.endOperation(70000, {59000, 58000, -1, -1});
        queue.beginOperation(VideoDecodeQueue::Operation::Reset, 1, -1, 71000);
        QVERIFY(queue.take());
        auto after = queue.resetContext();
        after["queueDepth"] = before.value("queueDepth");
        QCOMPARE(after, before);
        queue.reset("frame_gap", 20, 15, 81000);
        const auto reset = queue.resetContext();
        QCOMPARE(reset.value("workerOperation").toString(), QString("reset"));
        QCOMPARE(reset.value("workerElapsedMs").toDouble(), 10.0);
        QCOMPARE(reset.value("lastDecodeEpoch").toULongLong(), 0ULL);
        QCOMPARE(reset.value("lastDecodeDownloadMs").toDouble(), -1.0);
        QCOMPARE(reset.value("oldestQueueWaitMs").toDouble(), -1.0);
    }
    void burstHistoryIsBoundedAndIdleIsExplicit() {
        VideoDecodeQueue queue;
        for (int id = 0; id < 20; ++id) {
            QVERIFY(queue.push("frame", id * 1000, -1, id, id * 1000, 7));
            QVERIFY(queue.take());
        }
        for (int id = 20; id < 24; ++id)
            QVERIFY(queue.push("burst", 20000, -1, id, 20000 + id - 20, 8));
        const auto snapshot = queue.resetContext();
        QCOMPARE(snapshot.value("workerOperation").toString(), QString("idle"));
        QCOMPARE(snapshot.value("workerElapsedMs").toDouble(), -1.0);
        QCOMPARE(snapshot.value("lastDecodeTotalMs").toDouble(), -1.0);
        QCOMPARE(snapshot.value("lastDecodeFrameId").toInt(), -1);
        QCOMPARE(snapshot.value("enqueueBatchId").toULongLong(), 8ULL);
        QCOMPARE(snapshot.value("enqueueBatchUnits").toInt(), 4);
        QCOMPARE(snapshot.value("enqueueBatchSpanMs").toDouble(), 0.003);
        const auto ids = snapshot.value("recentEnqueueFrameIds").toList();
        QCOMPARE(ids.size(), 8);
        QCOMPARE(ids.first().toInt(), 16);
        QCOMPARE(ids.last().toInt(), 23);
        const auto intervals = snapshot.value("recentEnqueueIntervalsMs").toList();
        QCOMPARE(intervals.size(), 7);
        QCOMPARE(intervals.last().toDouble(), 0.001);
        const auto batches = snapshot.value("recentEnqueueBatchIds").toList();
        QCOMPARE(batches.size(), 8);
        QCOMPARE(batches.last().toULongLong(), 8ULL);
        queue.reset("source_timeout", -1, -1, 30000);
        QVERIFY(queue.push("new source", 31000, -1, 1, 31000, 9));
        queue.reset("frame_gap", 3, 1, 32000);
        // 超时与显式断链切断入队历史，不能把上个来源的节奏带入新来源。
        QCOMPARE(queue.resetContext().value("recentEnqueueFrameIds").toList(), QVariantList({1}));
    }
    void resetContextSurvivesConsumerTakingLatestUnit() {
        VideoDecodeQueue queue;
        for (int id = 1; id <= 4; ++id) QVERIFY(queue.push("frame", id * 1000, -1, id));
        const auto unit = queue.take();
        QVERIFY(unit.has_value());
        QCOMPARE(unit->frame_id, 4);
        auto context = queue.resetContext();
        QCOMPARE(context.value("reason").toString(), QString("queue_overflow"));
        QCOMPARE(context.value("queueDepthBefore").toInt(), 3);
        QCOMPARE(context.value("droppedByReset").toULongLong(), 3ULL);
        QCOMPARE(context.value("frameId").toInt(), 4);
        QCOMPARE(context.value("queueDepth").toInt(), 0);
        QVERIFY(queue.push("next", 5000, -1, 5));
        queue.reset("frame_gap", 7, 5);
        context = queue.resetContext();
        QCOMPARE(context.value("reason").toString(), QString("frame_gap"));
        QCOMPARE(context.value("previousFrameId").toInt(), 5);
        QCOMPARE(context.value("frameId").toInt(), 7);
        QCOMPARE(context.value("droppedByReset").toULongLong(), 1ULL);
        QCOMPARE(context.value("droppedTotal").toULongLong(), 4ULL);
    }
    void carriesBothClockOriginsWithoutMixingUnits() {
        VideoDecodeQueue queue;
        QVERIFY(queue.push("one", 5000, 1000));
        QVERIFY(queue.push("two", 6000, 2000));
        const auto first = queue.take();
        const auto second = queue.take();
        QVERIFY(first && second);
        QCOMPARE(first->received_us, 5000LL);
        QCOMPARE(first->first_packet_us, 1000LL);
        QCOMPARE(second->received_us, 6000LL);
        QCOMPARE(second->first_packet_us, 2000LL);
    }
    void blockedConsumerDoesNotBlockReception() {
        VideoDecodeQueue queue;
        const auto clock_us = [] {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
        std::latch entered(1);
        std::latch release(1);
        QByteArray consumed;
        QVERIFY(queue.push("first", 0));
        std::jthread consumer([&] {
            const auto first = queue.take();
            if (first) {
                consumed = first->bytes;
                queue.beginOperation(VideoDecodeQueue::Operation::Decode, first->epoch,
                                     first->frame_id, clock_us());
            }
            entered.count_down();
            release.wait();
            queue.endOperation(clock_us(), {11000, 11000, -1, -1});
        });
        entered.wait();
        // 消费者模拟停在驱动调用里；以下完成证明收包无需等待它。
        for (int i = 1; i <= 10; ++i) queue.push(QByteArray::number(i), i * 1000);
        const auto evidence = queue.resetContext();
        release.count_down();
        consumer.join();
        QCOMPARE(evidence.value("workerOperation").toString(), QString("decode"));
        QCOMPARE(evidence.value("lastDecodeTotalMs").toDouble(), -1.0);
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
