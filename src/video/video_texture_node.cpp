#include "video_texture_node.h"

#include <rhi/qrhi.h>

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QVector4D>
#include <array>
#include <chrono>

#include "video_color_converter.h"
#include "video_native_texture.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

namespace {
qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 初始化静态库 shader 资源；不依赖最终可执行文件恰好链接其他 qrc 符号。
}  // namespace
static void initializeVideoShaders() { Q_INIT_RESOURCE(video_shaders); }

namespace {
// 场景图在渲染准备阶段消费更新批次；QByteArray 自己拥有上传数据，绝不借用已回收的解码平面。
class PlaneTexture : public QSGTexture {
   public:
    std::unique_ptr<QRhiTexture> owned;
    QRhiTexture* texture = nullptr;
    QByteArray bytes;
    qint64 comparisonKey() const override { return qint64(quintptr(texture)); }
    QRhiTexture* rhiTexture() const override { return texture; }
    QSize textureSize() const override { return texture ? texture->pixelSize() : QSize{}; }
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }
    void commitTextureOperations(QRhi*, QRhiResourceUpdateBatch* updates) override {
        if (bytes.isEmpty() || !texture) return;
        updates->uploadTexture(texture,
                               QRhiTextureUploadDescription{{QRhiTextureUploadEntry(
                                   0, 0, QRhiTextureSubresourceUploadDescription(bytes))}});
        bytes.clear();
    }
    bool upload(QRhi* rhi, const AVFrame* frame, int plane, QRhiTexture::Format format,
                const QSize& size, int bytes_per_pixel) {
        const int row_bytes = size.width() * bytes_per_pixel;
        if (!frame->data[plane] || qAbs(qint64(frame->linesize[plane])) < row_bytes) return false;
        if (!owned || owned->pixelSize() != size || owned->format() != format) {
            owned.reset(rhi->newTexture(format, size));
            if (!owned->create()) {
                texture = nullptr;
                return false;
            }
        }
        texture = owned.get();
        bytes.resize(row_bytes * size.height());
        for (int y = 0; y < size.height(); ++y)
            memcpy(bytes.data() + y * row_bytes,
                   frame->data[plane] + qint64(y) * frame->linesize[plane], row_bytes);
        return true;
    }
    void borrow(QRhiTexture* native) {
        owned.reset();
        bytes.clear();
        texture = native;
    }
};

class YuvMaterial;
class YuvShader : public QSGMaterialShader {
   public:
    YuvShader() {
        setShaderFileName(VertexStage, ":/pip/video/shaders/video.vert.qsb");
        setShaderFileName(FragmentStage, ":/pip/video/shaders/video.frag.qsb");
    }
    bool updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) override;
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                            QSGMaterial* material, QSGMaterial*) override;
};

class YuvMaterial : public QSGMaterial {
   public:
    std::array<PlaneTexture, 3> planes;
    QMatrix4x4 color_matrix;
    QVector4D color_offset;
    QVector4D sampling;
    bool is_semiplanar = false;
    YuvMaterial() {
        // 每个节点持有独立可变纹理，不能与另一视频节点批处理或比较为相等。
        for (auto& plane : planes) plane.setFiltering(QSGTexture::Linear);
        setFlag(Blending, true);
    }
    QSGMaterialType* type() const override {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override {
        return new YuvShader;
    }
    int compare(const QSGMaterial* other) const override {
        return this == other ? 0 : (quintptr(this) < quintptr(other) ? -1 : 1);
    }
    void configureColor(const AVFrame* frame, AVPixelFormat format) {
        // 输入为 UNORM 采样值。10 位 planar 低位对齐；P010 高位对齐，不能混用量化系数。
        const bool is_ten_bit = format == AV_PIX_FMT_YUV420P10LE || format == AV_PIX_FMT_P010LE;
        is_semiplanar = format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010LE;
        const float max_value = is_ten_bit ? 1023.0f : 255.0f;
        const float storage_scale = !is_ten_bit                   ? 1.0f
                                    : format == AV_PIX_FMT_P010LE ? 65535.0f / (1023 * 64)
                                                                  : 65535.0f / 1023;
        const bool is_full = frame->color_range == AVCOL_RANGE_JPEG;
        const float y_min = is_full ? 0 : (is_ten_bit ? 64 : 16);
        const float y_range = is_full ? max_value : (is_ten_bit ? 876 : 219);
        const float c_mid = is_ten_bit ? 512 : 128;
        const float c_range = is_full ? max_value : (is_ten_bit ? 896 : 224);
        float kr = 0.2126f, kb = 0.0722f;  // 与 CPU 转色器一致：未标注沿用 BT.709。
        if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M) {
            kr = 0.299f;
            kb = 0.114f;
        } else if (frame->colorspace == AVCOL_SPC_BT2020_NCL) {
            kr = 0.2627f;
            kb = 0.0593f;
        }
        const float kg = 1 - kr - kb;
        const float rv = 2 * (1 - kr), bu = 2 * (1 - kb);
        const float gu = -2 * kb * (1 - kb) / kg, gv = -2 * kr * (1 - kr) / kg;
        const float ys = storage_scale * max_value / y_range;
        const float cs = storage_scale * max_value / c_range;
        color_matrix.fill(0);
        color_matrix(0, 0) = color_matrix(1, 0) = color_matrix(2, 0) = ys;
        color_matrix(0, 2) = rv * cs;
        color_matrix(1, 1) = gu * cs;
        color_matrix(1, 2) = gv * cs;
        color_matrix(2, 1) = bu * cs;
        color_offset = {-y_min / y_range - rv * c_mid / c_range,
                        -y_min / y_range - (gu + gv) * c_mid / c_range,
                        -y_min / y_range - bu * c_mid / c_range, 0};
        // UV 左采样相对于中心采样偏半个 luma 像素；单位为归一化源坐标。
        const bool is_left = frame->chroma_location == AVCHROMA_LOC_LEFT ||
                             frame->chroma_location == AVCHROMA_LOC_TOPLEFT ||
                             frame->chroma_location == AVCHROMA_LOC_BOTTOMLEFT;
        const float dy = frame->chroma_location == AVCHROMA_LOC_TOPLEFT ||
                                 frame->chroma_location == AVCHROMA_LOC_TOP
                             ? 0.5f / frame->height
                         : frame->chroma_location == AVCHROMA_LOC_BOTTOMLEFT ||
                                 frame->chroma_location == AVCHROMA_LOC_BOTTOM
                             ? -0.5f / frame->height
                             : 0;
        sampling = {is_left ? 0.5f / frame->width : 0, dy, is_semiplanar ? 1.0f : 0.0f, 0};
    }
};

bool YuvShader::updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) {
    auto* yuv = static_cast<YuvMaterial*>(material);
    auto* bytes = state.uniformData();
    // 与 shader std140 对齐：mat4 0/64、vec4 128/144、float 160；Qt 为块分配 padding。
    const auto matrix = state.combinedMatrix();
    memcpy(bytes->data(), matrix.constData(), 64);
    memcpy(bytes->data() + 64, yuv->color_matrix.constData(), 64);
    memcpy(bytes->data() + 128, &yuv->color_offset, 16);
    memcpy(bytes->data() + 144, &yuv->sampling, 16);
    const float opacity = state.opacity();
    memcpy(bytes->data() + 160, &opacity, 4);
    return true;
}

void YuvShader::updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                                   QSGMaterial* material, QSGMaterial*) {
    auto* yuv = static_cast<YuvMaterial*>(material);
    if (binding < 1 || binding > 3) return;
    const int index = binding == 3 && yuv->is_semiplanar ? 1 : binding - 1;
    auto& plane = yuv->planes[index];
    plane.commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    *texture = &plane;
}
}  // namespace

struct VideoTextureNode::Impl {
    VideoNativeTexture native;
    VideoColorConverter converter;        // 软件显示回退复用 swscale 缓存；仅场景图线程访问。
    QSGGeometryNode* yuv_node = nullptr;  // 子节点由 QSG 树拥有，不能在 Impl 析构时重复删除。
    QSGSimpleTextureNode* rgba_node = nullptr;
    QString path;
    QString fallback;
    std::shared_ptr<const AVFrame> rejected_pool_frame;
    QString rejection;
    qint64 download_us = -1, convert_us = -1, bridge_us = -1;
    void clearChildren(VideoTextureNode* owner) {
        while (auto* child = owner->firstChild()) {
            owner->removeChildNode(child);
            delete child;
        }
        yuv_node = nullptr;
        rgba_node = nullptr;
    }
};

VideoTextureNode::VideoTextureNode() : impl_(std::make_unique<Impl>()) {
    static const bool is_initialized = [] {
        initializeVideoShaders();
        return true;
    }();
    Q_UNUSED(is_initialized);
}
VideoTextureNode::~VideoTextureNode() {
    // 先销毁借用原生纹理的材质，再释放原生纹理及其设备；只在渲染线程执行。
    impl_->clearChildren(this);
}
void VideoTextureNode::clearFrame() {
    impl_->clearChildren(this);
    impl_ = std::make_unique<Impl>();
}
QString VideoTextureNode::path() const { return impl_->path; }
QString VideoTextureNode::fallbackReason() const { return impl_->fallback; }
qint64 VideoTextureNode::downloadUs() const { return impl_->download_us; }
qint64 VideoTextureNode::convertUs() const { return impl_->convert_us; }
qint64 VideoTextureNode::bridgeUs() const { return impl_->bridge_us; }

bool VideoTextureNode::setFrame(const VideoFrame& input, QQuickWindow* window) {
    auto& state = *impl_;
    state.download_us = state.convert_us = state.bridge_us = -1;
    state.fallback.clear();
    static const auto mode = [] {
        const auto requested = qEnvironmentVariable("PIP_VIDEO_PRESENTATION", "auto");
        if (requested == "auto" || requested == "yuv" || requested == "rgba") return requested;
        qWarning() << "未知 PIP_VIDEO_PRESENTATION，使用 auto：" << requested;
        return QString("auto");
    }();
    auto* rhi = static_cast<QRhi*>(
        window->rendererInterface()->getResource(window, QSGRendererInterface::RhiResource));
    auto frame = input;
    bool is_native = false;
    if (rhi && frame.pixels && frame.pixels->hw_frames_ctx && mode == "auto") {
        // 同一解码 surface 池共享失败后只回退，不逐帧创建资源/重试驱动。
        // 保留一个引用防止池地址被重用；新池或场景图重建会重新探测。
        if (state.rejected_pool_frame &&
            state.rejected_pool_frame->hw_frames_ctx->data == frame.pixels->hw_frames_ctx->data) {
            state.fallback = state.rejection;
        } else {
            const auto began = monotonicUs();
            is_native = state.native.update(frame, rhi, &state.fallback);
            state.bridge_us = monotonicUs() - began;
            state.rejected_pool_frame = is_native ? nullptr : frame.pixels;
            state.rejection = state.fallback;
        }
    } else if (!frame.pixels || !frame.pixels->hw_frames_ctx) {
        state.rejected_pool_frame.reset();
        state.rejection.clear();
    }
    if (!is_native && frame.pixels && frame.pixels->hw_frames_ctx) {
        const auto began = monotonicUs();
        QString error;
        frame = frame.downloaded(&error);
        state.download_us = monotonicUs() - began;
        if (frame.isNull()) {
            state.fallback = error;
            return false;
        }
        if (state.fallback.isEmpty())
            state.fallback = rhi ? "显式 YUV/RGBA 参考路径" : "软件场景图不支持硬件纹理";
    }
    auto format = frame.pixels ? AVPixelFormat(frame.pixels->format) : AV_PIX_FMT_NONE;
    if (is_native) {
        const auto* frames =
            reinterpret_cast<const AVHWFramesContext*>(frame.pixels->hw_frames_ctx->data);
        format = frames->sw_format;
    }
    const bool is_ten_bit = format == AV_PIX_FMT_YUV420P10LE || format == AV_PIX_FMT_P010LE;
    const bool is_semiplanar = format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010LE;
    const bool is_yuv = format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_NV12 || is_ten_bit;
    if (rhi && is_yuv && mode != "rgba") {
        if (!state.yuv_node) {
            state.clearChildren(this);
            state.yuv_node = new QSGGeometryNode;
            auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
            geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            state.yuv_node->setGeometry(geometry);
            state.yuv_node->setFlag(QSGNode::OwnsGeometry);
            state.yuv_node->setMaterial(new YuvMaterial);
            state.yuv_node->setFlag(QSGNode::OwnsMaterial);
            appendChildNode(state.yuv_node);
        }
        auto* material = static_cast<YuvMaterial*>(state.yuv_node->material());
        bool succeeded = true;
        for (int i = 0; i < (is_semiplanar ? 2 : 3); ++i) {
            if (is_native) {
                material->planes[i].borrow(state.native.plane(i));
                continue;
            }
            const QSize size =
                i == 0 ? frame.size()
                       : QSize((frame.size().width() + 1) / 2, (frame.size().height() + 1) / 2);
            const bool is_uv = is_semiplanar && i == 1;
            const auto plane_format = is_uv ? (is_ten_bit ? QRhiTexture::RG16 : QRhiTexture::RG8)
                                            : (is_ten_bit ? QRhiTexture::R16 : QRhiTexture::R8);
            succeeded &= material->planes[i].upload(rhi, frame.pixels.get(), i, plane_format, size,
                                                    (is_uv ? 2 : 1) * (is_ten_bit ? 2 : 1));
        }
        if (succeeded) {
            material->configureColor(frame.pixels.get(), format);
            state.yuv_node->markDirty(QSGNode::DirtyMaterial);
            state.path = is_native ? state.native.path() : "yuv-upload";
            return true;
        }
        state.fallback = "RHI 平面纹理创建/上传失败";
    }
    if (!state.rgba_node) {
        state.clearChildren(this);
        state.rgba_node = new QSGSimpleTextureNode;
        state.rgba_node->setOwnsTexture(true);
        state.rgba_node->setFiltering(QSGTexture::Linear);
        appendChildNode(state.rgba_node);
    }
    const auto began = monotonicUs();
    QString error;
    auto image = frame.image;
    if (image.isNull() && frame.pixels) {
        image = state.converter.convert(frame.pixels.get());
        error = state.converter.error();
        state.convert_us = monotonicUs() - began;
    }
    auto* texture = image.isNull() ? nullptr : window->createTextureFromImage(image);
    if (!texture) {
        state.fallback = error.isEmpty() ? "RGBA 纹理创建失败" : error;
        return false;
    }
    state.rgba_node->setTexture(texture);
    state.path = "rgba-upload";
    if (state.fallback.isEmpty() && mode != "rgba")
        state.fallback = rhi ? "像素格式不支持平面 shader" : "软件场景图";
    return true;
}

void VideoTextureNode::setRect(const QRectF& rect) {
    if (impl_->yuv_node) {
        QSGGeometry::updateTexturedRectGeometry(impl_->yuv_node->geometry(), rect, {0, 0, 1, 1});
        impl_->yuv_node->markDirty(QSGNode::DirtyGeometry);
    }
    if (impl_->rgba_node) impl_->rgba_node->setRect(rect);
}
