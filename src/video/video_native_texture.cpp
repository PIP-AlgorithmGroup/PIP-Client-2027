#include "video_native_texture.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <array>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

#ifdef Q_OS_WIN
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}
using Microsoft::WRL::ComPtr;
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <QLibrary>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <deque>
extern "C" {
#include <libavutil/hwcontext_drm.h>
}
#endif

struct VideoNativeTexture::Impl {
    std::array<std::unique_ptr<QRhiTexture>, 2> planes;
    QString path;
#ifdef Q_OS_WIN
    ComPtr<ID3D11Texture2D> shared_source;
    ComPtr<ID3D11Texture2D> shared_target;
    ComPtr<ID3D11Texture2D> local;
    ComPtr<IDXGIKeyedMutex> source_mutex;
    ComPtr<IDXGIKeyedMutex> target_mutex;
    ID3D11Device* source_device = nullptr;  // shared_source 强引用其 device，不借用已释放设备。
    ID3D11Device* target_device = nullptr;
    QSize size;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    // 超时后不再复用未知 key/设备状态的共享资源；下次重新创建并再次探测。
    void clear() {
        planes = {};
        local.Reset();
        shared_target.Reset();
        shared_source.Reset();
        source_mutex.Reset();
        target_mutex.Reset();
        source_device = target_device = nullptr;
    }
    bool updateD3d(const VideoFrame& frame, QRhi* rhi, QString* reason);
#else
    QLibrary egl{"EGL", 1};
    PFNEGLGETCURRENTDISPLAYPROC current_display = nullptr;
    PFNEGLQUERYSTRINGPROC query_string = nullptr;
    PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
    using ImageTarget = void (*)(GLenum, void*);
    ImageTarget image_target = nullptr;
    struct Pending {
        VideoFrame frame;  // 保留 DRM fd、映射和底层 VA surface，直至复制 fence 完成。
        EGLDisplay display = EGL_NO_DISPLAY;
        std::array<EGLImageKHR, 2> images{EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR};
        std::array<GLuint, 2> textures{};
        GLsync fence = nullptr;
    };
    std::deque<Pending> pending;
    bool updateEgl(const VideoFrame& frame, QRhi* rhi, QString* reason);
    void release(Pending& value, QOpenGLExtraFunctions* gl) {
        if (gl) {
            if (value.fence) gl->glDeleteSync(value.fence);
            gl->glDeleteTextures(2, value.textures.data());
        }
        for (auto image : value.images)
            if (image != EGL_NO_IMAGE_KHR) destroy_image(value.display, image);
    }
    ~Impl() {
        auto* context = QOpenGLContext::currentContext();
        auto* gl = context ? context->extraFunctions() : nullptr;
        // 场景图销毁时不释放 GPU 仍在读取的解码 surface；仅退出/重建路径允许完成等待。
        if (gl && !pending.empty()) gl->glFinish();
        for (auto& value : pending) release(value, gl);
    }
#endif
};

VideoNativeTexture::VideoNativeTexture() : impl_(std::make_unique<Impl>()) {}
VideoNativeTexture::~VideoNativeTexture() = default;
QRhiTexture* VideoNativeTexture::plane(int index) const { return impl_->planes.at(index).get(); }
QString VideoNativeTexture::path() const { return impl_->path; }

bool VideoNativeTexture::update(const VideoFrame& frame, QRhi* rhi, QString* reason) {
    reason->clear();
    if (!frame.pixels || !frame.pixels->hw_frames_ctx || !rhi) {
        *reason = "没有可导入的硬件帧或 RHI";
        return false;
    }
#ifdef Q_OS_WIN
    return impl_->updateD3d(frame, rhi, reason);
#else
    return impl_->updateEgl(frame, rhi, reason);
#endif
}

#ifdef Q_OS_WIN
bool VideoNativeTexture::Impl::updateD3d(const VideoFrame& frame, QRhi* rhi, QString* reason) {
    const auto fail = [&](const QString& message) {
        *reason = message;
        clear();
        return false;
    };
    if (rhi->backend() != QRhi::D3D11 || frame.pixels->format != AV_PIX_FMT_D3D11)
        return fail("硬件帧与 Qt 后端不匹配（需 D3D11VA + D3D11）");
    const auto* handles = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
    auto* target = static_cast<ID3D11Device*>(handles->dev);
    auto* target_context = static_cast<ID3D11DeviceContext*>(handles->context);
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frame.pixels->hw_frames_ctx->data);
    auto* device = static_cast<AVD3D11VADeviceContext*>(frames->device_ctx->hwctx);
    auto* source = reinterpret_cast<ID3D11Texture2D*>(frame.pixels->data[0]);
    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    if ((source_desc.Format != DXGI_FORMAT_NV12 && source_desc.Format != DXGI_FORMAT_P010) ||
        frame.size().width() % 2 || frame.size().height() % 2)
        return fail("D3D11 平面导入仅支持偶数尺寸 NV12/P010");
    if (source_device != device->device || target_device != target || size != frame.size() ||
        format != source_desc.Format) {
        clear();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = frame.size().width();
        desc.Height = frame.size().height();
        desc.MipLevels = desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.Format = source_desc.Format;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        HRESULT result = device->device->CreateTexture2D(&desc, nullptr, &shared_source);
        if (FAILED(result))
            return fail(QString("D3D11 共享纹理创建失败：%1").arg(quint32(result), 0, 16));
        ComPtr<IDXGIResource> resource;
        HANDLE shared_handle = nullptr;
        if (FAILED(shared_source.As(&resource)) ||
            FAILED(resource->GetSharedHandle(&shared_handle)) ||
            FAILED(target->OpenSharedResource(shared_handle, IID_PPV_ARGS(&shared_target))) ||
            FAILED(shared_source.As(&source_mutex)) || FAILED(shared_target.As(&target_mutex)))
            return fail("D3D11 跨设备共享失败，检查解码/显示适配器是否一致");
        // 此处为 DXGI legacy shared handle，API 契约明确不能 CloseHandle。
        desc.MiscFlags = 0;
        if (FAILED(target->CreateTexture2D(&desc, nullptr, &local)))
            return fail("D3D11 本地采样纹理创建失败");
        const bool is_ten_bit = desc.Format == DXGI_FORMAT_P010;
        for (int i = 0; i < 2; ++i) {
            const auto plane_format = i == 0 ? (is_ten_bit ? QRhiTexture::R16 : QRhiTexture::R8)
                                             : (is_ten_bit ? QRhiTexture::RG16 : QRhiTexture::RG8);
            const QSize plane_size = i == 0 ? frame.size() : frame.size() / 2;
            planes[i].reset(rhi->newTexture(plane_format, plane_size));
            // 同一 NV12/P010 资源的不同格式 SRV 分别选择 Y/UV 平面；不把数组 slice 当 2D。
            if (!planes[i]->createFrom({quint64(local.Get()), 0}))
                return fail("D3D11 平面 SRV 创建失败");
        }
        source_device = device->device;
        target_device = target;
        size = frame.size();
        format = source_desc.Format;
    }
    // 只锁 FFmpeg immediate context 的复制提交，不能持有接收邮箱/压缩队列锁。
    // keyed mutex key 0 属于生产者、1 属于消费者；等待上限 5ms，失败走可诊断回退。
    if (source_mutex->AcquireSync(0, 5) != S_OK) return fail("D3D11 生产者同步超时/设备失效");
    device->lock(device->lock_ctx);
    const D3D11_BOX crop{0, 0, 0, UINT(size.width()), UINT(size.height()), 1};
    device->device_context->CopySubresourceRegion(
        shared_source.Get(), 0, 0, 0, 0, source,
        UINT(reinterpret_cast<intptr_t>(frame.pixels->data[1])), &crop);
    const auto released_source = source_mutex->ReleaseSync(1);
    device->unlock(device->lock_ctx);
    if (released_source != S_OK) return fail("D3D11 生产者释放同步失败");
    if (target_mutex->AcquireSync(1, 5) != S_OK) return fail("D3D11 消费者同步超时/设备失效");
    target_context->CopyResource(local.Get(), shared_target.Get());
    if (target_mutex->ReleaseSync(0) != S_OK) return fail("D3D11 消费者释放同步失败");
    path = "d3d11-shared-yuv";
    return true;
}
#else
bool VideoNativeTexture::Impl::updateEgl(const VideoFrame& frame, QRhi* rhi, QString* reason) {
    const auto fail = [&](const QString& message) {
        *reason = message;
        return false;
    };
    auto* context = QOpenGLContext::currentContext();
    if (rhi->backend() != QRhi::OpenGLES2 || !context ||
        (frame.pixels->format != AV_PIX_FMT_VAAPI && frame.pixels->format != AV_PIX_FMT_DRM_PRIME))
        return fail("硬件帧与 Qt 后端不匹配（需 VAAPI/DRM PRIME + EGL OpenGL）");
    // 不依赖 Qt Multimedia 私有解码器或 libva SDK；FFmpeg 负责导出并同步 VA surface。
    // AV_HWFRAME_MAP_READ 内部可能调用 vaSyncSurface，没有可移植的超时/取消保证；
    // 其全部 CPU 等待计入 gpuBridge，不能把零像素下载误报成零同步成本。
    if (!current_display) {
        current_display =
            reinterpret_cast<PFNEGLGETCURRENTDISPLAYPROC>(egl.resolve("eglGetCurrentDisplay"));
        query_string = reinterpret_cast<PFNEGLQUERYSTRINGPROC>(egl.resolve("eglQueryString"));
        const auto get_proc =
            reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(egl.resolve("eglGetProcAddress"));
        if (!current_display || !query_string || !get_proc) return fail("无法加载 EGL 入口");
        create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(get_proc("eglCreateImageKHR"));
        destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(get_proc("eglDestroyImageKHR"));
        image_target = reinterpret_cast<ImageTarget>(get_proc("glEGLImageTargetTexture2DOES"));
    }
    if (!create_image || !destroy_image || !image_target) return fail("缺少 EGLImage 导入入口");
    const auto display = current_display();
    if (display == EGL_NO_DISPLAY) return fail("Qt 当前 OpenGL 上下文不是 EGL（可能为 GLX）");
    const QByteArray extensions(query_string(display, EGL_EXTENSIONS));
    if (!extensions.contains("EGL_EXT_image_dma_buf_import"))
        return fail("EGL 不支持 dma-buf 导入");
    auto* gl = context->extraFunctions();
    const auto version = context->format();
    // GLES 3 有核心 fence；桌面 OpenGL 3.0/3.1 还需要 ARB_sync，不能仅凭主版本调用入口。
    const bool has_sync = context->isOpenGLES()
                              ? version.majorVersion() >= 3
                              : version.majorVersion() > 3 ||
                                    (version.majorVersion() == 3 && version.minorVersion() >= 2) ||
                                    context->hasExtension("GL_ARB_sync");
    if (version.majorVersion() < 3 || !has_sync)
        return fail("EGL 硬件复制需要 OpenGL/GLES 3 与可用 fence");
    while (!pending.empty()) {
        const auto status = gl->glClientWaitSync(pending.front().fence, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) break;
        release(pending.front(), gl);
        pending.pop_front();
    }
    if (pending.size() >= 6) return fail("EGL 待完成复制达到 6 帧上限");
    Pending value;
    value.frame = frame;
    if (frame.pixels->format == AV_PIX_FMT_VAAPI) {
        auto* mapped = av_frame_alloc();
        if (!mapped) return fail("无法分配 DRM 映射帧");
        mapped->format = AV_PIX_FMT_DRM_PRIME;
        const int result =
            av_hwframe_map(mapped, frame.pixels.get(), AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
        if (result < 0) {
            av_frame_free(&mapped);
            return fail(QString("VAAPI DRM PRIME 直接导出失败：%1").arg(result));
        }
        value.frame.pixels = std::shared_ptr<const AVFrame>(mapped, [](const AVFrame* frame) {
            auto* owned = const_cast<AVFrame*>(frame);
            av_frame_free(&owned);
        });
    }
    const auto* descriptor =
        reinterpret_cast<const AVDRMFrameDescriptor*>(value.frame.pixels->data[0]);
    // FFmpeg VAAPI 的 SEPARATE_LAYERS 输出；拒绝未验证的多平面/压缩布局，不猜测 modifier。
    constexpr quint32 kR8 = 'R' | ('8' << 8) | (' ' << 16) | (' ' << 24);
    constexpr quint32 kGr88 = 'G' | ('R' << 8) | ('8' << 16) | ('8' << 24);
    if (!descriptor || descriptor->nb_layers != 2 || descriptor->layers[0].format != kR8 ||
        descriptor->layers[1].format != kGr88)
        return fail("EGL 平面导入仅接受 R8/GR88 NV12 独立层");
    value.display = display;
    gl->glGenTextures(2, value.textures.data());
    GLint old_texture = 0, old_read_fbo = 0;
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fbo);
    GLuint read_fbo = 0;
    gl->glGenFramebuffers(1, &read_fbo);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    QString failure;
    for (int i = 0; i < 2 && failure.isEmpty(); ++i) {
        const auto& layer = descriptor->layers[i];
        if (layer.nb_planes != 1 || layer.planes[0].object_index < 0 ||
            layer.planes[0].object_index >= descriptor->nb_objects) {
            failure = "DRM 平面引用非法";
            break;
        }
        const auto& plane = layer.planes[0];
        const auto& object = descriptor->objects[plane.object_index];
        const QSize size =
            i == 0 ? frame.size()
                   : QSize((frame.size().width() + 1) / 2, (frame.size().height() + 1) / 2);
        std::vector<EGLint> attributes{EGL_WIDTH,
                                       size.width(),
                                       EGL_HEIGHT,
                                       size.height(),
                                       EGL_LINUX_DRM_FOURCC_EXT,
                                       EGLint(layer.format),
                                       EGL_DMA_BUF_PLANE0_FD_EXT,
                                       object.fd,
                                       EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                                       EGLint(plane.offset),
                                       EGL_DMA_BUF_PLANE0_PITCH_EXT,
                                       EGLint(plane.pitch)};
        if (object.format_modifier != 0 && object.format_modifier != ((quint64(1) << 56) - 1)) {
            if (!extensions.contains("EGL_EXT_image_dma_buf_import_modifiers")) {
                failure = "EGL 不支持该 DRM modifier";
                break;
            }
            attributes.insert(
                attributes.end(),
                {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGLint(object.format_modifier),
                 EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGLint(object.format_modifier >> 32)});
        }
        attributes.push_back(EGL_NONE);
        value.images[i] = create_image(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
                                       attributes.data());
        if (value.images[i] == EGL_NO_IMAGE_KHR) {
            failure = "EGLImage 格式/modifier 导入失败";
            break;
        }
        gl->glBindTexture(GL_TEXTURE_2D, value.textures[i]);
        image_target(GL_TEXTURE_2D, value.images[i]);
        gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   value.textures[i], 0);
        if (gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            failure = "EGL 导入平面不可作为复制源";
            break;
        }
        const auto format = i == 0 ? QRhiTexture::R8 : QRhiTexture::RG8;
        if (!planes[i] || planes[i]->pixelSize() != size) {
            planes[i].reset(rhi->newTexture(format, size));
            if (!planes[i]->create()) {
                failure = "无法创建 EGL 复制目标";
                break;
            }
        }
        gl->glBindTexture(GL_TEXTURE_2D, GLuint(planes[i]->nativeTexture().object));
        gl->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size.width(), size.height());
        if (gl->glGetError() != GL_NO_ERROR) failure = "EGL 平面 GPU 复制失败";
    }
    // 恢复 Qt 的原生状态；fence 后保留源 surface，不能在仅提交复制时释放解码引用。
    gl->glBindTexture(GL_TEXTURE_2D, GLuint(old_texture));
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(old_read_fbo));
    gl->glDeleteFramebuffers(1, &read_fbo);
    value.fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl->glFlush();
    if (!value.fence) {
        gl->glFinish();
        release(value, gl);
        return fail("EGL fence 创建失败");
    }
    pending.push_back(std::move(value));
    if (!failure.isEmpty()) return fail(failure);
    path = "vaapi-egl-yuv";
    return true;
}
#endif
