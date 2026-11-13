#include "ffmpeg_log_bridge.h"

#include <QLoggingCategory>
#include <QString>
#include <chrono>
#include <mutex>

extern "C" {
#include <libavutil/log.h>
}

namespace {
Q_LOGGING_CATEGORY(ffmpeg_log, "pip.ffmpeg")
std::mutex rate_mutex;
auto period = std::chrono::steady_clock::now();
int emitted = 0;
quint64 suppressed = 0;

void forwardLog(void* context, int level, const char* format, va_list arguments) {
    if (level > AV_LOG_WARNING) return;
    quint64 previous_suppressed = 0;
    {
        std::lock_guard lock(rate_mutex);
        const auto now = std::chrono::steady_clock::now();
        if (now - period >= std::chrono::seconds(1)) {
            period = now;
            emitted = 0;
            previous_suppressed = suppressed;
            suppressed = 0;
        }
        if (emitted++ >= 8) {
            ++suppressed;
            return;
        }
    }
    if (previous_suppressed)
        qCWarning(ffmpeg_log) << "FFmpeg repeated messages suppressed:" << previous_suppressed;
    // FFmpeg 可从任意解码/驱动线程回调；前缀状态按线程隔离，固定缓冲防止坏码流刷屏。
    thread_local int prefix = 1;
    char line[2048]{};
    av_log_format_line2(context, level, format, arguments, line, sizeof(line), &prefix);
    const auto message = QString::fromUtf8(line).trimmed();
    if (level <= AV_LOG_ERROR)
        qCCritical(ffmpeg_log).noquote() << message;
    else
        qCWarning(ffmpeg_log).noquote() << message;
}
}  // namespace

FfmpegLogBridge::FfmpegLogBridge() { av_log_set_callback(forwardLog); }
FfmpegLogBridge::~FfmpegLogBridge() { av_log_set_callback(av_log_default_callback); }
