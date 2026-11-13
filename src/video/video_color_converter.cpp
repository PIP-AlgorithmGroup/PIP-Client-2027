#include "video_color_converter.h"

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

struct VideoColorConverter::Impl {
    AVFrame* planar = av_frame_alloc();
    SwsContext* unpack = nullptr;
    SwsContext* rgba = nullptr;
    QString error;
    ~Impl() {
        av_frame_free(&planar);
        sws_freeContext(unpack);
        sws_freeContext(rgba);
    }
};

VideoColorConverter::VideoColorConverter() : impl_(std::make_unique<Impl>()) {}
VideoColorConverter::~VideoColorConverter() = default;
QString VideoColorConverter::error() const { return impl_->error; }

QImage VideoColorConverter::convert(const AVFrame* frame) {
    auto& state = *impl_;
    state.error.clear();
    if (!frame || frame->width <= 0 || frame->height <= 0 || frame->width > 1920 ||
        frame->height > 1080) {
        state.error = "转色输入尺寸无效（最大 1920×1080）";
        return {};
    }
    const auto* source = frame;
    if (frame->format == AV_PIX_FMT_NV12) {
        if (!state.planar) {
            state.error = "无法分配 NV12 平面转换帧";
            return {};
        }
        auto* planar = state.planar;
        if (planar->width != frame->width || planar->height != frame->height || !planar->buf[0]) {
            av_frame_unref(planar);
            planar->format = AV_PIX_FMT_YUV420P;
            planar->width = frame->width;
            planar->height = frame->height;
            if (av_frame_get_buffer(planar, 32) < 0) {
                state.error = "无法分配 NV12 拆平面缓存";
                return {};
            }
        }
        // FFmpeg 7 的等尺寸 NV12→RGBA 不进入平面 YUV 的专用快速转换器。
        // 先拆交错 UV，不改变像素、位深、色彩范围或采样位置；仅复用本线程一张中间帧。
        state.unpack = sws_getCachedContext(
            state.unpack, frame->width, frame->height, AV_PIX_FMT_NV12, frame->width, frame->height,
            AV_PIX_FMT_YUV420P, SWS_POINT, nullptr, nullptr, nullptr);
        if (!state.unpack || sws_scale(state.unpack, frame->data, frame->linesize, 0, frame->height,
                                       planar->data, planar->linesize) != frame->height) {
            state.error = "NV12 拆平面失败";
            return {};
        }
        source = planar;
    }
    state.rgba = sws_getCachedContext(
        state.rgba, source->width, source->height, static_cast<AVPixelFormat>(source->format),
        source->width, source->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!state.rgba) {
        state.error = "无法创建视频色彩转换器";
        return {};
    }
    // 使用原帧标记；拆平面缓存不拥有流元数据，不能因改布局丢失矩阵或范围。
    const int matrix =
        frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M
            ? SWS_CS_ITU601
        : frame->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020
                                                    : SWS_CS_ITU709;
    const auto* coefficients = sws_getCoefficients(matrix);
    if (sws_setColorspaceDetails(state.rgba, coefficients, frame->color_range == AVCOL_RANGE_JPEG,
                                 coefficients, 1, 0, 1 << 16, 1 << 16) < 0) {
        state.error = "无法配置视频色彩矩阵和范围";
        return {};
    }
    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
    if (image.isNull()) {
        state.error = "无法分配视频画面";
        return {};
    }
    uint8_t* planes[] = {image.bits(), nullptr, nullptr, nullptr};
    int strides[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    if (sws_scale(state.rgba, source->data, source->linesize, 0, source->height, planes, strides) !=
        source->height) {
        state.error = "视频色彩转换未完成";
        return {};
    }
    return image;
}
