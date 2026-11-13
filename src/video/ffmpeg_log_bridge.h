#pragma once

/**
 * 应用独占 FFmpeg 全局日志回调的 RAII 守卫；在所有视频/探测线程之前创建，之后析构。
 * 将 FFmpeg warning/error 接入 Qt 日志，最多每秒 8 条并记录抑制数量；不输出逐帧 debug。
 * 同进程不能与其他 av_log_set_callback 管理者混用；析构恢复 FFmpeg 默认 callback。
 */
class FfmpegLogBridge {
   public:
    FfmpegLogBridge();
    ~FfmpegLogBridge();
    FfmpegLogBridge(const FfmpegLogBridge&) = delete;
    FfmpegLogBridge& operator=(const FfmpegLogBridge&) = delete;
};
