#include "video/video_statistics.h"

#include <QTest>
#include <cmath>

/** 注入同一单调微秒时基，统计不依赖计时器精度或主机解码速度。 */
class VideoStatisticsTest : public QObject {
    Q_OBJECT
   private slots:
    void heartbeatSeparatesTimerIntervalFromGuiDelay() {
        VideoTimingStatistics timings;
        timings.heartbeat(0);
        timings.heartbeat(20000);
        timings.heartbeat(140000);
        auto heartbeat = timings.snapshot(140000).value("guiHeartbeat").toMap();
        QCOMPARE(heartbeat.value("samples").toInt(), 2);
        QCOMPARE(heartbeat.value("maxMs").toDouble(), 100.0);
        QCOMPARE(heartbeat.value("meanMs").toDouble(), 50.0);
        timings.reset();
        timings.heartbeat(2000000);
        QCOMPARE(timings.snapshot(2000000).value("guiHeartbeat").toMap().value("samples").toInt(),
                 0);
    }
    void stageTimingsKeepPercentilesAndExpire() {
        VideoTimingStatistics timings;
        for (int i = 1; i <= 100; ++i) timings.record(VideoStage::Codec, i * 1000, 500000);
        auto codec = timings.snapshot(500000).value("codec").toMap();
        QCOMPARE(codec.value("meanMs").toDouble(), 50.5);
        QCOMPARE(codec.value("p95Ms").toDouble(), 95.0);
        QCOMPARE(codec.value("p99Ms").toDouble(), 99.0);
        QCOMPARE(codec.value("maxMs").toDouble(), 100.0);
        QCOMPARE(timings.snapshot(500000).value("download").toMap().value("meanMs").toDouble(),
                 -1.0);
        QCOMPARE(timings.snapshot(1500000).value("codec").toMap().value("samples").toInt(), 0);
        for (int i = 0; i < 10000; ++i) timings.record(VideoStage::Codec, 10, 2000000);
        QVERIFY(timings.snapshot(2000000).value("codec").toMap().value("samples").toInt() <= 512);
        timings.reset();
        QCOMPARE(timings.snapshot(2000000).value("codec").toMap().value("samples").toInt(), 0);
    }
    void startupAndIdleAreNotMeasuredAsSixty() {
        VideoStatistics stats;
        const auto startup = stats.snapshot(0);
        QCOMPARE(startup.value("receiveFps").toDouble(), -1.0);
        QCOMPARE(startup.value("low1Fps").toDouble(), -1.0);
        QCOMPARE(startup.value("frameLossPercent").toDouble(), -1.0);
        const auto idle = stats.snapshot(1000000);
        QCOMPARE(idle.value("receiveFps").toDouble(), 0.0);
        QCOMPARE(idle.value("frameLossPercent").toDouble(), -1.0);
    }

    void stableRateAndDecodeCost() {
        VideoStatistics stats;
        for (int i = 0; i <= 120; ++i) {
            const qint64 now = i * 16667LL;
            stats.datagram(1000, now);
            stats.completeFrame(static_cast<quint16>(i), now);
            stats.decodeFinished(4000, true, now);
        }
        const auto result = stats.snapshot(2000000);
        QVERIFY(std::abs(result.value("receiveFps").toDouble() - 60.0) < 1.1);
        QVERIFY(std::abs(result.value("decodeFps").toDouble() - 60.0) < 1.1);
        QVERIFY(std::abs(result.value("low5Fps").toDouble() - 59.9988) < 0.01);
        QVERIFY(std::abs(result.value("low1Fps").toDouble() - 59.9988) < 0.01);
        QCOMPARE(result.value("decodeMs").toDouble(), 4.0);
        QCOMPARE(result.value("frameLossPercent").toDouble(), 0.0);
        QVERIFY(result.value("megabitsPerSecond").toDouble() > 0);
        // 不能把帧序号缺口或默认 1392 负载当作可精确测得的包损。
        QCOMPARE(result.value("packetLossPercent").toDouble(), -1.0);
    }

    void lowsRequireEnoughSamplesAndExposeStalls() {
        VideoStatistics stats;
        for (int i = 0; i <= 20; ++i) stats.completeFrame(i, i * 10000LL);
        auto result = stats.snapshot(200000);
        QCOMPARE(result.value("low5Fps").toDouble(), 100.0);
        QCOMPARE(result.value("low1Fps").toDouble(), -1.0);
        for (int i = 21; i <= 100; ++i) stats.completeFrame(i, i * 10000LL);
        stats.completeFrame(101, 1200000);
        result = stats.snapshot(1200000);
        // 101 间隔中最慢 ceil(1.01)=2 个：200ms 和 10ms，1% low = 1000/105。
        QVERIFY(std::abs(result.value("low1Fps").toDouble() - 1000.0 / 105) < 0.001);
        QVERIFY(result.value("low5Fps").toDouble() < 100);
        result = stats.snapshot(2200000);
        QCOMPARE(result.value("receiveFps").toDouble(), 0.0);
        QVERIFY(result.value("low1Fps").toDouble() < 10);
        QCOMPARE(stats.snapshot(12000000).value("low1Fps").toDouble(), -1.0);
    }

    void decodeMaximumExposesTailAndExpires() {
        VideoStatistics stats;
        stats.decodeFinished(4000, true, 0);
        stats.decodeFinished(80000, true, 20000);
        stats.decodeFinished(6000, false, 500000);
        const auto result = stats.snapshot(500000);
        QCOMPARE(result.value("decodeMs").toDouble(), 30.0);
        QCOMPARE(result.value("decodeMaxMs").toDouble(), 80.0);
        // 最大值和均值均遵守 1 秒窗口，不能一直保留历史最差值。
        QCOMPARE(stats.snapshot(1030000).value("decodeMaxMs").toDouble(), 6.0);
        QCOMPARE(stats.snapshot(1500000).value("decodeMaxMs").toDouble(), -1.0);
    }

    void frameGapsWrapAndWindowExpires() {
        VideoStatistics stats;
        stats.completeFrame(65534, 0);
        stats.completeFrame(1, 1000000);  // 缺 65535、0；归到检测到缺口的窗口。
        QCOMPARE(stats.snapshot(1000000).value("frameLossPercent").toDouble(), 200.0 / 3);
        stats.completeFrame(2, 1500000);
        QCOMPARE(stats.snapshot(1500000).value("frameLossPercent").toDouble(), 50.0);
        QCOMPARE(stats.snapshot(2500000).value("frameLossPercent").toDouble(), -1.0);
        // 来源重建后不能把上一来源的序号与帧间隔带入新会话。
        stats.reset(3000000);
        stats.completeFrame(10, 3000000);
        QCOMPARE(stats.snapshot(3000000).value("low5Fps").toDouble(), -1.0);
        QCOMPARE(stats.snapshot(3000000).value("frameLossPercent").toDouble(), -1.0);
    }

    void countersAndStorageAreBounded() {
        VideoStatistics stats;
        for (int i = 0; i < 30000; ++i) {
            stats.datagram(1400, i);
            stats.completeFrame(static_cast<quint16>(i), i);
        }
        stats.assemblyCounters(2, 3, 4, 30000);
        const auto result = stats.snapshot(1000000);
        QCOMPARE(result.value("invalidPackets").toULongLong(), 2ULL);
        QCOMPARE(result.value("duplicatePackets").toULongLong(), 3ULL);
        QCOMPARE(result.value("incompleteFrames").toULongLong(), 4ULL);
        QVERIFY(result.value("lowSampleCount").toInt() <= 4096);
        const auto expired = stats.snapshot(2000000);
        QCOMPARE(expired.value("invalidPackets").toULongLong(), 0ULL);
    }
};
QTEST_GUILESS_MAIN(VideoStatisticsTest)
#include "video_statistics_test.moc"
