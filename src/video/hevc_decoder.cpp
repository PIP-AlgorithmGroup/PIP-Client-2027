#include "hevc_decoder.h"

#include <QStringList>
#include <algorithm>
#include <chrono>
#include <vector>

#include "video_color_converter.h"
#include "video_decode_devices.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
}

namespace {
// 每个完整 AU 对应的本机时钟；FFmpeg 引用计数复制到该 AU 实际输出的帧，支持延迟/重排。
struct FrameOrigin {
    qint64 first_packet_us;
    qint64 assembled_us;
};
// RAII 累加同次调用内的多个输出/回退尝试；异常或提前返回也不会漏掉已执行阶段。
class StageTimer {
   public:
    explicit StageTimer(qint64& duration_us)
        : duration_us_(duration_us), began_(std::chrono::steady_clock::now()) {}
    ~StageTimer() {
        duration_us_ =
            qMax(qint64(0), duration_us_) + std::chrono::duration_cast<std::chrono::microseconds>(
                                                std::chrono::steady_clock::now() - began_)
                                                .count();
    }

   private:
    qint64& duration_us_;
    std::chrono::steady_clock::time_point began_;
};
QString ffmpegError(int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, message, sizeof(message));
    return QString::fromUtf8(message);
}
}  // namespace

struct HevcDecoder::Impl {
    AVCodecContext* context = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVFrame* downloaded = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    VideoColorConverter converter;
    QString error;
    struct Candidate {
        AVHWDeviceType type;
        QString id;
        QString label;
    };
    std::vector<Candidate> candidates;
    size_t next_candidate = 0;        // 单会话只前进，坏设备不会在每个 AU 上反复初始化。
    AVBufferRef* hardware = nullptr;  // 本线程拥有；codec 另持引用，先释放 codec 再释放设备。
    AVPixelFormat hardware_format = AV_PIX_FMT_NONE;
    QString selected = "CPU";
    QString device;
    QStringList failures;
    bool confirmed = false;
    bool format_failed = false;
    VideoDecodeTimings timings;

    // FFmpeg 回调只允许所选硬件格式；拒绝静默软解，否则诊断会把 CPU 误报成 GPU。
    static AVPixelFormat selectFormat(AVCodecContext* context, const AVPixelFormat* formats) {
        auto& state = *static_cast<Impl*>(context->opaque);
        for (; *formats != AV_PIX_FMT_NONE; ++formats)
            if (*formats == state.hardware_format) return *formats;
        state.format_failed = true;
        return AV_PIX_FMT_NONE;
    }
    bool open();
    void advance(const QString& reason);
    ~Impl() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&downloaded);
        avcodec_free_context(&context);
        av_buffer_unref(&hardware);
    }
};

bool HevcDecoder::Impl::open() {
    format_failed = false;
    const auto* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    avcodec_free_context(&context);
    if (!codec || !frame || !downloaded || !packet || !(context = avcodec_alloc_context3(codec))) {
        error = "无法分配 HEVC 解码器";
        return false;
    }
    // FFmpeg 7 的 HEVC slice/WPP 并行曾在 1080p 实流中停在 avcodec_send_packet，
    // 导致 GUI stop/configure 的 join 永不完成。基础路径禁用编解码器内部并行，
    // 仍由独立解码线程执行解码；帧率按设备实测，不以增加线程数换取不可取消等待。
    context->thread_count = 1;
    context->thread_type = 0;
    context->pkt_timebase = AVRational{1, 1000000};
    context->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
    context->max_pixels = 1920 * 1080;
    context->err_recognition = AV_EF_CAREFUL;
    // 错误经设置面板节流展示；不允许坏包触发 FFmpeg 每帧向控制台刷屏。
    context->log_level_offset = 128;
    if (hardware) {
        context->hw_device_ctx = av_buffer_ref(hardware);
        if (!context->hw_device_ctx) {
            error = "无法引用硬件解码设备";
            return false;
        }
        context->opaque = this;
        context->get_format = selectFormat;
    }
    const auto result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        error = "HEVC 初始化失败：" + ffmpegError(result);
        avcodec_free_context(&context);
        return false;
    }
    error.clear();
    return true;
}

void HevcDecoder::Impl::advance(const QString& reason) {
    if (!reason.isEmpty()) failures.append(selected + " / " + device + ": " + reason);
    av_frame_unref(frame);
    av_frame_unref(downloaded);
    avcodec_free_context(&context);
    av_buffer_unref(&hardware);
    confirmed = false;
    hardware_format = AV_PIX_FMT_NONE;
    const auto* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    while (codec && next_candidate < candidates.size()) {
        const auto candidate = candidates[next_candidate++];
        const auto* name = av_hwdevice_get_type_name(candidate.type);
        selected = QString::fromLatin1(name);
        device = candidate.label;
        for (int i = 0; const auto* config = avcodec_get_hw_config(codec, i); ++i) {
            if (config->device_type == candidate.type &&
                (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
                hardware_format = config->pix_fmt;
                break;
            }
        }
        if (hardware_format == AV_PIX_FMT_NONE) {
            failures.append(selected + ": FFmpeg HEVC 不包含该硬件后端");
            continue;
        }
        const auto id = candidate.id.toUtf8();
        const int result = av_hwdevice_ctx_create(
            &hardware, candidate.type, id.isEmpty() ? nullptr : id.constData(), nullptr, 0);
        if (result >= 0 && open()) return;
        failures.append(selected + " / " + device + ": " +
                        (result < 0 ? ffmpegError(result) : error));
        avcodec_free_context(&context);
        av_buffer_unref(&hardware);
        hardware_format = AV_PIX_FMT_NONE;
    }
    selected = "CPU";
    device = "CPU";
    hardware_format = AV_PIX_FMT_NONE;
    open();
}

HevcDecoder::HevcDecoder(const QString& backend, const QString& device)
    : impl_(std::make_unique<Impl>()) {
    auto& state = *impl_;
    if (!state.frame || !state.downloaded || !state.packet) {
        state.error = "无法分配 HEVC 帧或数据包";
        return;
    }
    if (backend == "auto" || backend == "selected") {
        // 保存的设备身份重新映射到本次驱动编号，避免插拔改变 GPU 排序后选错设备。
        for (const auto& item : videoDecodeDevices()) {
            if (backend == "selected" && item.key != device) continue;
            const auto type = av_hwdevice_find_type_by_name(item.backend.toUtf8().constData());
            if (type != AV_HWDEVICE_TYPE_NONE)
                state.candidates.push_back({type, item.device, item.label});
        }
        if (backend == "selected" && state.candidates.empty())
            state.failures.append(device + ": 已保存设备不存在，回退 CPU");
    } else if (backend != "cpu") {
        const auto type = av_hwdevice_find_type_by_name(backend.toUtf8().constData());
        if (type == AV_HWDEVICE_TYPE_NONE) {
            state.failures.append(backend + ": 未知 FFmpeg 硬件后端");
        } else {
#ifdef Q_OS_WIN
            if (backend == "d3d11va" && !device.isEmpty()) {
                // FFmpeg 可能将无效 DXGI 编号替换为默认 GPU；先校验精确编号。
                const auto devices = videoDecodeDevices();
                const bool exists =
                    std::any_of(devices.begin(), devices.end(), [&](const auto& item) {
                        return item.backend == backend && item.device == device;
                    });
                if (!exists) {
                    state.failures.append(backend + " / " + device + ": 不存在的 DXGI 适配器编号");
                    state.advance({});
                    return;
                }
            }
#endif
            state.candidates.push_back({type, device, device.isEmpty() ? "默认设备" : device});
        }
    }
    state.advance({});
}

HevcDecoder::~HevcDecoder() = default;

QString HevcDecoder::error() const { return impl_->error; }
bool HevcDecoder::hardwareActive() const { return impl_->confirmed; }
QString HevcDecoder::backend() const {
    return impl_->selected + (impl_->hardware && !impl_->confirmed ? "?" : "");
}
QString HevcDecoder::device() const { return impl_->device; }
QString HevcDecoder::fallbackReason() const { return impl_->failures.join("; "); }
VideoDecodeTimings HevcDecoder::timings() const { return impl_->timings; }

QVariantList HevcDecoder::probeDevices() {
    QVariantList result;
    result.push_back(QVariantMap{{"key", "cpu"},
                                 {"backend", "cpu"},
                                 {"device", "cpu"},
                                 {"label", videoCpuName() + " · 软件解码"},
                                 {"available", true},
                                 {"hardwareActive", false},
                                 {"reason", "始终可用的 CPU 基础路径"}});
    for (const auto& device : videoDecodeDevices()) {
        HevcDecoder decoder(device.backend, device.device);
        const bool initialized = decoder.backend() != "CPU";
        result.push_back(QVariantMap{{"key", device.key},
                                     {"backend", device.backend},
                                     {"device", device.device},
                                     {"label", device.label},
                                     {"available", initialized},
                                     {"hardwareActive", false},
                                     {"reason", initialized ? "已创建解码上下文，需真实 HEVC 帧确认"
                                                            : decoder.fallbackReason()}});
    }
    return result;
}
void HevcDecoder::reset() {
    // 重开 codec 以清除旧参数集；保留 device 引用，避免网络抖动反复创建驱动设备。
    av_frame_unref(impl_->frame);
    av_frame_unref(impl_->downloaded);
    impl_->confirmed = false;
    if (!impl_->open() && impl_->hardware) impl_->advance(impl_->error);
}

VideoFrame HevcDecoder::decode(const QByteArray& access_unit, qint64 assembled_us,
                               qint64 first_packet_us) {
    impl_->timings = {};
    StageTimer total(impl_->timings.total_us);
    return decodeAttempt(access_unit, assembled_us, first_packet_us);
}

VideoFrame HevcDecoder::decodeAttempt(const QByteArray& access_unit, qint64 assembled_us,
                                      qint64 first_packet_us) {
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
    // 只作本机耗时关联，不重写媒体播放时钟；重排/延迟输出携带自己的 AU 起点。
    state.packet->pts = assembled_us >= 0 ? assembled_us : AV_NOPTS_VALUE;
    state.packet->opaque_ref = av_buffer_alloc(sizeof(FrameOrigin));
    if (!state.packet->opaque_ref) {
        state.error = "无法分配视频时间关联元数据";
        return output;
    }
    const FrameOrigin origin{first_packet_us, assembled_us};
    memcpy(state.packet->opaque_ref->data, &origin, sizeof(origin));
    {
        StageTimer timer(state.timings.codec_us);
        result = avcodec_send_packet(state.context, state.packet);
    }
    if (result < 0) {
        state.error = "HEVC 数据不可解码：" + ffmpegError(result);
        if (state.hardware && (state.format_failed || result != AVERROR_INVALIDDATA)) {
            // 只重试当前有界 AU；不缓存历史 GOP。无参数集的后继帧等待下一个关键帧恢复。
            state.advance(state.error);
            return decodeAttempt(access_unit, assembled_us, first_packet_us);
        }
        return output;
    }
    const auto receive_frame = [&] {
        StageTimer timer(state.timings.codec_us);
        return avcodec_receive_frame(state.context, state.frame);
    };
    while ((result = receive_frame()) >= 0) {
        const AVFrame* frame = state.frame;
        if (frame->width <= 0 || frame->height <= 0 || frame->width > 1920 ||
            frame->height > 1080 || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
            state.error = "丢弃异常 HEVC 画面（最大 1920×1080）";
            av_frame_unref(state.frame);
            continue;
        }
        if (state.hardware) {
            if (frame->format != state.hardware_format) {
                state.advance("未输出所选硬件像素格式");
                return decodeAttempt(access_unit, assembled_us, first_packet_us);
            }
            av_frame_unref(state.downloaded);
            {
                StageTimer timer(state.timings.download_us);
                result = av_hwframe_transfer_data(state.downloaded, frame, 0);
                if (result >= 0) result = av_frame_copy_props(state.downloaded, frame);
            }
            if (result < 0) {
                state.advance("硬件帧下载失败：" + ffmpegError(result));
                return decodeAttempt(access_unit, assembled_us, first_packet_us);
            }
            // 下载到 CPU 后沿用 RGBA 显示接口；这不是零拷贝，属性复制保留色彩矩阵和 SAR。
            state.confirmed = true;
            frame = state.downloaded;
        }
        StageTimer convert_timer(state.timings.convert_us);
        auto image = state.converter.convert(frame);
        if (!image.isNull()) {
            output.image = std::move(image);
            const auto sar = frame->sample_aspect_ratio;
            output.pixel_aspect = sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1.0;
            output.assembled_us = frame->pts == AV_NOPTS_VALUE ? -1 : frame->pts;
            if (frame->opaque_ref && frame->opaque_ref->size == sizeof(FrameOrigin)) {
                FrameOrigin actual;
                memcpy(&actual, frame->opaque_ref->data, sizeof(actual));
                output.first_packet_us = actual.first_packet_us;
                output.assembled_us = actual.assembled_us;
            }
            state.error.clear();
        } else {
            state.error = state.converter.error();
        }
        av_frame_unref(state.frame);
    }
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        state.error = "HEVC 接收画面失败：" + ffmpegError(result);
        if (state.hardware && (state.format_failed || result != AVERROR_INVALIDDATA)) {
            state.advance(state.error);
            return decodeAttempt(access_unit, assembled_us, first_packet_us);
        }
    }
    return output;
}
