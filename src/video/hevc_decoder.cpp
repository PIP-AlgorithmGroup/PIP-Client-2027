#include "hevc_decoder.h"

#include <QDir>
#include <QStringList>
#include <algorithm>
#include <vector>
#ifdef Q_OS_WIN
#include <dxgi1_2.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
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
    AVFrame* downloaded = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    SwsContext* scaler = nullptr;
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
        sws_freeContext(scaler);
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
    if (backend == "auto"
#ifdef Q_OS_WIN
        || backend == "d3d11va"
#endif
    ) {
#ifdef Q_OS_WIN
        // 按 DXGI 的真实适配器索引传给 FFmpeg，覆盖混合显卡；跳过软件适配器。
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(
                CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
            IDXGIAdapter1* adapter = nullptr;
            for (UINT i = 0; i < 16 && factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
                DXGI_ADAPTER_DESC1 description{};
                if (SUCCEEDED(adapter->GetDesc1(&description)) &&
                    !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                    state.candidates.push_back({AV_HWDEVICE_TYPE_D3D11VA, QString::number(i),
                                                QString::fromWCharArray(description.Description) +
                                                    " [DXGI " + QString::number(i) + "]"});
                adapter->Release();
            }
            factory->Release();
        }
        if (state.candidates.empty())
            state.candidates.push_back({AV_HWDEVICE_TYPE_D3D11VA, {}, "默认适配器"});
        if (backend == "d3d11va" && !device.isEmpty()) {
            // FFmpeg 在无效编号时会使用默认 GPU；先对真实枚举校验，防止悄悄换设备。
            const auto found = std::find_if(state.candidates.begin(), state.candidates.end(),
                                            [&](const auto& item) { return item.id == device; });
            if (found == state.candidates.end()) {
                state.failures.append("d3d11va / " + device + ": 不存在的 DXGI 适配器编号");
                state.candidates.clear();
            } else {
                const auto candidate = *found;
                state.candidates = {candidate};
            }
        }
#elif defined(Q_OS_LINUX)
        const auto nodes =
            QDir("/dev/dri").entryList({"renderD*"}, QDir::System | QDir::Files, QDir::Name);
        for (const auto& node : nodes.mid(0, 16)) {
            const auto path = "/dev/dri/" + node;
            state.candidates.push_back({AV_HWDEVICE_TYPE_VAAPI, path, path});
        }
        if (state.candidates.empty())
            state.candidates.push_back({AV_HWDEVICE_TYPE_VAAPI, {}, "默认 render 节点"});
#endif
        if (backend == "auto")
            state.candidates.push_back({AV_HWDEVICE_TYPE_CUDA, {}, "默认 NVIDIA CUDA 设备"});
    } else if (backend != "cpu") {
        const auto type = av_hwdevice_find_type_by_name(backend.toUtf8().constData());
        if (type == AV_HWDEVICE_TYPE_NONE)
            state.failures.append(backend + ": 未知 FFmpeg 硬件后端");
        else
            state.candidates.push_back({type, device, device.isEmpty() ? "默认设备" : device});
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
void HevcDecoder::reset() {
    // 重开 codec 以清除旧参数集；保留 device 引用，避免网络抖动反复创建驱动设备。
    av_frame_unref(impl_->frame);
    av_frame_unref(impl_->downloaded);
    impl_->confirmed = false;
    if (!impl_->open() && impl_->hardware) impl_->advance(impl_->error);
}

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
        if (state.hardware && (state.format_failed || result != AVERROR_INVALIDDATA)) {
            // 只重试当前有界 AU；不缓存历史 GOP。无参数集的后继帧等待下一个关键帧恢复。
            state.advance(state.error);
            return decode(access_unit);
        }
        return output;
    }
    while ((result = avcodec_receive_frame(state.context, state.frame)) >= 0) {
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
                return decode(access_unit);
            }
            av_frame_unref(state.downloaded);
            result = av_hwframe_transfer_data(state.downloaded, frame, 0);
            if (result >= 0) result = av_frame_copy_props(state.downloaded, frame);
            if (result < 0) {
                state.advance("硬件帧下载失败：" + ffmpegError(result));
                return decode(access_unit);
            }
            // 下载到 CPU 后沿用 RGBA 显示接口；这不是零拷贝，属性复制保留色彩矩阵和 SAR。
            state.confirmed = true;
            frame = state.downloaded;
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
        if (state.hardware && (state.format_failed || result != AVERROR_INVALIDDATA)) {
            state.advance(state.error);
            return decode(access_unit);
        }
    }
    return output;
}
