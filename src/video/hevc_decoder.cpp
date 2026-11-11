#include "hevc_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {
QString ffmpegError(int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, message, sizeof(message));
    return QString::fromUtf8(message);
}
}  // namespace

struct HevcDecoder::Impl {
    AVCodecContext* context = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    SwsContext* scaler = nullptr;
    QString error;
    ~Impl() {
        sws_freeContext(scaler);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&context);
    }
};

HevcDecoder::HevcDecoder() : impl_(std::make_unique<Impl>()) {
    auto& state = *impl_;
    const auto* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    if (!codec || !state.frame || !state.packet ||
        !(state.context = avcodec_alloc_context3(codec))) {
        state.error = "无法分配 HEVC 解码器";
        return;
    }
    // Slice 并行不增加 frame threading 的排队延迟；跨平台不要求任何厂商 GPU。
    state.context->thread_count = 4;
    state.context->thread_type = FF_THREAD_SLICE;
    state.context->max_pixels = 1920 * 1080;
    state.context->err_recognition = AV_EF_CAREFUL;
    // 错误经设置面板节流展示；不允许坏包触发 FFmpeg 每帧向控制台刷屏。
    state.context->log_level_offset = 128;
    const auto result = avcodec_open2(state.context, codec, nullptr);
    if (result < 0) {
        state.error = "HEVC 初始化失败：" + ffmpegError(result);
        avcodec_free_context(&state.context);
    }
}

HevcDecoder::~HevcDecoder() = default;

QString HevcDecoder::error() const { return impl_->error; }

VideoFrame HevcDecoder::decode(const QByteArray& access_unit) {
    auto& state = *impl_;
    VideoFrame output;
    if (!state.context || access_unit.isEmpty()) {
        return output;
    }
    av_packet_unref(state.packet);
    auto result = av_new_packet(state.packet, static_cast<int>(access_unit.size()));
    if (result < 0) {
        state.error = "码流内存分配失败：" + ffmpegError(result);
        return output;
    }
    // FFmpeg 要求 packet 尾部 padding；av_new_packet 提供它，不能直接借用 QByteArray。
    memcpy(state.packet->data, access_unit.constData(), access_unit.size());
    result = avcodec_send_packet(state.context, state.packet);
    if (result < 0) {
        state.error = "HEVC 数据不可解码：" + ffmpegError(result);
        return output;
    }
    while ((result = avcodec_receive_frame(state.context, state.frame)) >= 0) {
        const auto* frame = state.frame;
        if (frame->width <= 0 || frame->height <= 0 || frame->width > 1920 ||
            frame->height > 1080 || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
            state.error = "丢弃异常 HEVC 画面（最大 1920×1080）";
            av_frame_unref(state.frame);
            continue;
        }
        state.scaler = sws_getCachedContext(
            state.scaler, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!state.scaler) {
            state.error = "无法创建视频色彩转换器";
            av_frame_unref(state.frame);
            continue;
        }
        // 明确 YUV 范围和矩阵，避免把有限范围当全范围；未标注的 HD 使用 BT.709。
        const int matrix =
            frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M
                ? SWS_CS_ITU601
            : frame->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020
                                                        : SWS_CS_ITU709;
        const auto* coefficients = sws_getCoefficients(matrix);
        sws_setColorspaceDetails(state.scaler, coefficients, frame->color_range == AVCOL_RANGE_JPEG,
                                 coefficients, 1, 0, 1 << 16, 1 << 16);
        QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
        if (image.isNull()) {
            state.error = "无法分配视频画面";
            av_frame_unref(state.frame);
            continue;
        }
        uint8_t* planes[] = {image.bits(), nullptr, nullptr, nullptr};
        int strides[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
        const int rows = sws_scale(state.scaler, frame->data, frame->linesize, 0, frame->height,
                                   planes, strides);
        if (rows == frame->height) {
            output.image = std::move(image);
            const auto sar = frame->sample_aspect_ratio;
            output.pixel_aspect = sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1.0;
            state.error.clear();
        } else {
            state.error = "视频色彩转换未完成";
        }
        av_frame_unref(state.frame);
    }
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        state.error = "HEVC 接收画面失败：" + ffmpegError(result);
    }
    return output;
}
