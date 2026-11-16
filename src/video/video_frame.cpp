#include "hevc_decoder.h"
#include "video_color_converter.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

bool VideoFrame::isNull() const { return image.isNull() && !pixels; }
QSize VideoFrame::size() const {
    return pixels ? QSize(pixels->width, pixels->height) : image.size();
}

VideoFrame VideoFrame::downloaded(QString* error) const {
    if (error) error->clear();
    if (!pixels || !pixels->hw_frames_ctx) return *this;
    auto* software = av_frame_alloc();
    int result = software ? av_hwframe_transfer_data(software, pixels.get(), 0) : AVERROR(ENOMEM);
    if (result >= 0) result = av_frame_copy_props(software, pixels.get());
    if (result < 0) {
        av_frame_free(&software);
        if (error) {
            char message[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(result, message, sizeof(message));
            *error = "硬件帧下载失败：" + QString::fromUtf8(message);
        }
        return {};
    }
    auto output = *this;
    output.pixels = std::shared_ptr<const AVFrame>(software, [](const AVFrame* frame) {
        auto* owned = const_cast<AVFrame*>(frame);
        av_frame_free(&owned);
    });
    return output;
}

QImage VideoFrame::toImage(QString* error) const {
    if (error) error->clear();
    if (!image.isNull()) return image;
    const auto software = downloaded(error);
    if (!software.pixels) return {};
    VideoColorConverter converter;
    auto output = converter.convert(software.pixels.get());
    if (error) *error = converter.error();
    return output;
}
