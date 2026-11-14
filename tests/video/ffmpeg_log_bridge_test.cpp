#include "video/ffmpeg_log_bridge.h"

#include <QScopeGuard>
#include <QTest>

extern "C" {
#include <libavutil/log.h>
}

namespace {
QStringList captured;
void capture(QtMsgType, const QMessageLogContext& context, const QString& message) {
    if (QString::fromUtf8(context.category) == "pip.ffmpeg") captured.append(message);
}
}  // namespace

/** 真实 FFmpeg callback 接入测试；只在测试主线程调用，不创建图形/视频设备。 */
class FfmpegLogBridgeTest : public QObject {
    Q_OBJECT
   private slots:
    void capturesErrorsAndBoundsRepeatedMessages() {
        const auto previous = qInstallMessageHandler(capture);
        const auto restore = qScopeGuard([&] { qInstallMessageHandler(previous); });
        FfmpegLogBridge bridge;
        av_log(nullptr, AV_LOG_ERROR, "decoder failure %d\n", 17);
        QVERIFY(captured.contains("decoder failure 17"));
        const auto errors = captured.size();
        av_log(nullptr, AV_LOG_INFO, "per frame debug must not be persisted\n");
        QCOMPARE(captured.size(), errors);
        for (int i = 0; i < 100; ++i) av_log(nullptr, AV_LOG_WARNING, "repeated failure\n");
        QVERIFY(captured.size() <= 8);
    }
};

QTEST_GUILESS_MAIN(FfmpegLogBridgeTest)
#include "ffmpeg_log_bridge_test.moc"
