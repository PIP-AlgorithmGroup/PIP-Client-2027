#include "presentation_window.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <QFile>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QPlatformSurfaceEvent>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QTimer>
#include <functional>

namespace {
QQuickRenderControl* makeControl() {
    // 离屏/最小平台没有可呈交的原生 surface；保留 Qt 软件/测试循环，不能创建假交换链。
    const auto platform = QGuiApplication::platformName();
    if (platform == "offscreen" || platform == "minimal" ||
        QQuickWindow::sceneGraphBackend() == "software")
        return nullptr;
    const auto api = QQuickWindow::graphicsApi();
    return api == QSGRendererInterface::Direct3D11 || api == QSGRendererInterface::OpenGL
               ? new QQuickRenderControl
               : nullptr;
}
QShader loadShader(const QString& name) {
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}

// 只转发合并唤醒，不捕获窗口/接收器。GUI 清空 target 后，工作线程退出不会再接收新任务。
struct RenderWake {
    QMutex mutex;
    QObject* target = nullptr;
    bool is_queued = false;
    std::function<void()> render;
    void request(const std::shared_ptr<RenderWake>& self) {
        QMutexLocker lock(&mutex);
        if (!target || is_queued) return;
        is_queued = true;
        QMetaObject::invokeMethod(
            target,
            [self] {
                std::function<void()> operation;
                {
                    QMutexLocker lock(&self->mutex);
                    self->is_queued = false;
                    if (self->target) operation = self->render;
                }
                if (operation) operation();
            },
            Qt::QueuedConnection);
    }
};

// 所有 GPU/场景图资源只在呈交线程读写。should_sync=true 仅由已 polish 且阻塞的 GUI 请求。
class ScenePresenter : public QObject {
   public:
    PresentationWindow* window;
    QQuickRenderControl* control;
    QOffscreenSurface* fallback;
    QSize pixel_size;
    qreal dpr = 1;
    bool is_exposed = false;
    QString error;
    std::function<void(const QString&)> report_error;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QRhiSwapChain> swapchain;
    std::unique_ptr<QRhiRenderBuffer> swap_depth, depth;
    std::unique_ptr<QRhiRenderPassDescriptor> swap_pass, texture_pass;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiBuffer> orientation;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    bool has_scene = false;

    bool initialize() {
        if (rhi) return true;
        if (window->graphicsApi() == QSGRendererInterface::OpenGL) {
            QRhiGles2InitParams params;
            params.fallbackSurface = fallback;
            params.window = window;
            rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
        }
#ifdef Q_OS_WIN
        else {
            QRhiD3D11InitParams params;
            rhi.reset(QRhi::create(QRhi::D3D11, &params));
        }
#endif
        if (!rhi) return fail("独立呈交无法初始化图形设备");
        window->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(rhi.get()));
        if (!control->initialize()) return fail("独立呈交无法初始化 Qt 场景图");
        swapchain.reset(rhi->newSwapChain());
        swap_depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, {}, 1,
                                              QRhiRenderBuffer::UsedWithSwapChainOnly));
        swapchain->setWindow(window);
        swapchain->setDepthStencil(swap_depth.get());
        auto flags = QRhiSwapChain::Flags(QRhiSwapChain::UsedAsTransferSource);
        if (window->format().swapInterval() == 0) flags |= QRhiSwapChain::NoVSync;
        swapchain->setFlags(flags);
        swap_pass.reset(swapchain->newCompatibleRenderPassDescriptor());
        swapchain->setRenderPassDescriptor(swap_pass.get());
        sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        orientation.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
        return (sampler->create() && orientation->create()) || fail("独立呈交采样资源创建失败");
    }
    bool fail(const QString& message) {
        if (error != message && report_error) report_error(message);
        error = message;
        return false;
    }
    bool resizeTarget() {
        if (texture && texture->pixelSize() == pixel_size &&
            window->renderTarget().devicePixelRatio() == dpr)
            return true;
        window->setRenderTarget({});
        // GPU 安全销毁由 RHI 延迟回收；先释放引用该纹理的绑定/target，再替换纹理。
        pipeline.reset();
        bindings.reset();
        target.reset();
        texture_pass.reset();
        depth.reset();
        texture.reset(
            rhi->newTexture(QRhiTexture::RGBA8, pixel_size, 1,
                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixel_size, 1));
        if (!texture->create() || !depth->create()) return fail("独立呈交画面纹理创建失败");
        QRhiTextureRenderTargetDescription description{QRhiColorAttachment(texture.get())};
        description.setDepthStencilBuffer(depth.get());
        target.reset(rhi->newTextureRenderTarget(description));
        texture_pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(texture_pass.get());
        if (!target->create()) return fail("独立呈交离屏目标创建失败");
        auto render_target = QQuickRenderTarget::fromRhiRenderTarget(target.get());
        render_target.setDevicePixelRatio(dpr);
        window->setRenderTarget(render_target);
        bindings.reset(rhi->newShaderResourceBindings());
        bindings->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                      orientation.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                       texture.get(), sampler.get())});
        if (!bindings->create()) return fail("独立呈交纹理绑定失败");
        pipeline.reset(rhi->newGraphicsPipeline());
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, loadShader(":/pip/video/shaders/present.vert.qsb")},
             {QRhiShaderStage::Fragment, loadShader(":/pip/video/shaders/present.frag.qsb")}});
        pipeline->setShaderResourceBindings(bindings.get());
        pipeline->setRenderPassDescriptor(swap_pass.get());
        return pipeline->create() || fail("独立呈交管线创建失败");
    }
    void render(bool should_sync, QRhiReadbackResult* capture = nullptr) {
        if (!is_exposed || pixel_size.isEmpty() || !error.isEmpty()) return;
        // 首次初始化/重建只能发生在 GUI 已阻塞的同步入口；早到的视频唤醒不碰 QML 状态。
        if (!should_sync && !has_scene) return;
        if (!initialize() || !resizeTarget()) return;
        // GUI 不在场时仅重绘已同步的场景图；视频节点 preprocess 直接消费解码邮箱。
        // HUD 的 QML 对象不在此线程读取，且不会向 GUI 发 BlockingQueuedConnection。
        control->beginFrame();
        if (!rhi->isRecordingFrame()) {
            fail("独立呈交无法开始场景帧，图形设备可能已丢失");
            return;
        }
        if (should_sync) {
            control->sync();
            has_scene = true;
        }
        control->render();
        control->endFrame();
        if (rhi->isDeviceLost()) {
            fail("独立呈交图形设备已丢失");
            return;
        }
        if (swapchain->currentPixelSize() != swapchain->surfacePixelSize())
            if (!swapchain->createOrResize()) {
                fail("独立呈交交换链调整失败");
                return;
            }
        auto result = rhi->beginFrame(swapchain.get());
        if (result == QRhi::FrameOpSwapChainOutOfDate) {
            if (!swapchain->createOrResize()) {
                fail("独立呈交交换链失效");
                return;
            }
            result = rhi->beginFrame(swapchain.get());
        }
        if (result != QRhi::FrameOpSuccess) {
            fail("独立呈交设备/帧准备失败");
            return;
        }
        auto* commands = swapchain->currentFrameCommandBuffer();
        const auto size = swapchain->currentPixelSize();
        // 离屏采样原点与 NDC 的上下方向可能不同（例如 D3D11）；只修正最终三角形位置。
        const float direction[4] = {rhi->isYUpInNDC() == rhi->isYUpInFramebuffer() ? 1.0f : -1.0f,
                                    0, 0, 0};
        auto* updates = rhi->nextResourceUpdateBatch();
        updates->updateDynamicBuffer(orientation.get(), 0, sizeof(direction), direction);
        commands->beginPass(swapchain->currentFrameRenderTarget(), Qt::black, {1, 0}, updates);
        commands->setGraphicsPipeline(pipeline.get());
        commands->setViewport({0, 0, float(size.width()), float(size.height())});
        commands->setShaderResources();
        commands->draw(3);
        commands->endPass();
        if (capture) {
            // 默认描述读取当前交换链而非离屏纹理，覆盖最终合成/方向/通道；仅显式截图下载。
            auto* readback = rhi->nextResourceUpdateBatch();
            readback->readBackTexture(QRhiReadbackDescription(), capture);
            commands->resourceUpdate(readback);
        }
        if (rhi->endFrame(swapchain.get()) != QRhi::FrameOpSuccess) {
            fail("独立呈交提交失败");
            return;
        }
        emit window->frameSwapped();
    }
    QImage grab() {
        QRhiReadbackResult result;
        render(false, &result);
        if (!rhi) return {};
        // 读回结果是异步的；等待 GPU 只允许发生在显式截图，不进入视频常态路径。
        rhi->finish();
        if (result.data.isEmpty()) return {};
        const auto format = result.format == QRhiTexture::BGRA8
                                ? QImage::Format_ARGB32_Premultiplied
                                : QImage::Format_RGBA8888_Premultiplied;
        auto image = QImage(reinterpret_cast<const uchar*>(result.data.constData()),
                            result.pixelSize.width(), result.pixelSize.height(), format)
                         .copy();
        if (rhi->isYUpInFramebuffer()) image = image.flipped(Qt::Vertical);
        image.setDevicePixelRatio(dpr);
        return image;
    }
    void shutdown() {
        if (rhi) {
            control->invalidate();
            rhi->finish();
        }
        // QQuickWindow 借用 target/device；必须在对应 GPU 资源销毁前撤回借用。
        window->setRenderTarget({});
        window->setGraphicsDevice({});
        pipeline.reset();
        bindings.reset();
        target.reset();
        texture_pass.reset();
        texture.reset();
        depth.reset();
        sampler.reset();
        orientation.reset();
        swapchain.reset();
        swap_pass.reset();
        swap_depth.reset();
        rhi.reset();
        has_scene = false;
    }
};
}  // namespace

struct PresentationWindow::Impl {
    QQuickRenderControl* control = nullptr;  ///< 本窗口 GUI 拥有，GPU 操作由 worker 独占。
    QThread thread;
    ScenePresenter* presenter = nullptr;
    std::unique_ptr<QOffscreenSurface> fallback;
    std::shared_ptr<RenderWake> wake = std::make_shared<RenderWake>();
    QPointer<VideoReceiver> source;
    QMetaObject::Connection source_connection;
    bool is_sync_pending = false;
    bool is_stopping = false;
    bool is_recovery_pending = false;
    int recovery_attempts = 0;
    QString error;
};

PresentationWindow::PresentationWindow(QWindow* parent) : PresentationWindow(makeControl()) {
    setParent(parent);
}
PresentationWindow::PresentationWindow(QQuickRenderControl* control)
    : QQuickWindow(control), impl_(std::make_unique<Impl>()) {
    impl_->control = control;
    if (!control) return;
    control->setParent(this);
    setSurfaceType(graphicsApi() == QSGRendererInterface::OpenGL ? QSurface::OpenGLSurface
                                                                 : QSurface::Direct3DSurface);
    auto format = QSurfaceFormat::defaultFormat();
    setFormat(format);
    if (graphicsApi() == QSGRendererInterface::OpenGL)
        impl_->fallback.reset(QRhiGles2InitParams::newFallbackSurface(format));
    auto* presenter = new ScenePresenter;
    presenter->window = this;
    presenter->control = control;
    presenter->fallback = impl_->fallback.get();
    presenter->report_error = [this](const QString& error) {
        // 错误也通过值消息回 GUI；驱动失败不能等待 GUI，或逐帧重复打印。
        QMetaObject::invokeMethod(
            this,
            [this, error] {
                if (impl_->error != error) {
                    impl_->error = error;
                    qCritical().noquote() << error;
                    emit presentationErrorChanged();
                }
                scheduleRecovery();
            },
            Qt::QueuedConnection);
    };
    impl_->presenter = presenter;
    presenter->moveToThread(&impl_->thread);
    control->prepareThread(&impl_->thread);
    impl_->wake->target = presenter;
    impl_->wake->render = [presenter] { presenter->render(false); };
    connect(&impl_->thread, &QThread::finished, presenter, &QObject::deleteLater);
    connect(control, &QQuickRenderControl::sceneChanged, this, &PresentationWindow::scheduleSync);
    connect(control, &QQuickRenderControl::renderRequested, this,
            &PresentationWindow::scheduleSync);
    impl_->thread.setObjectName("video-presentation");
    impl_->thread.start();
}
PresentationWindow::~PresentationWindow() { stopRendering(); }
bool PresentationWindow::independentPresentation() const { return impl_->control != nullptr; }
VideoReceiver* PresentationWindow::videoSource() const { return impl_->source; }
QString PresentationWindow::presentationError() const { return impl_->error; }
void PresentationWindow::setVideoSource(VideoReceiver* receiver) {
    if (impl_->source == receiver) return;
    disconnect(impl_->source_connection);
    impl_->source = receiver;
    if (receiver && impl_->control) {
        const auto wake = impl_->wake;
        impl_->source_connection = connect(
            receiver, &VideoReceiver::renderFrameAvailable, this, [wake] { wake->request(wake); },
            Qt::DirectConnection);
    }
    emit videoSourceChanged();
    scheduleSync();
}
void PresentationWindow::scheduleSync() {
    if (!impl_->control || impl_->is_stopping || impl_->is_sync_pending) return;
    impl_->is_sync_pending = true;
    QTimer::singleShot(0, this, [this] {
        impl_->is_sync_pending = false;
        syncScene();
    });
}
void PresentationWindow::syncScene() {
    if (!impl_->control || impl_->is_stopping || !isExposed()) return;
    impl_->control->polishItems();
    const auto pixels = size() * devicePixelRatio();
    const auto dpr = devicePixelRatio();
    auto* presenter = impl_->presenter;
    QString presentation_error;
    QMetaObject::invokeMethod(
        presenter,
        [presenter, pixels, dpr, &presentation_error] {
            presenter->pixel_size = pixels;
            presenter->dpr = dpr;
            presenter->is_exposed = true;
            presenter->render(true);
            // 阻塞调用只保护此任务；返回后呈交线程可继续写错误，GUI 只能读取值快照。
            presentation_error = presenter->error;
        },
        Qt::BlockingQueuedConnection);
    if (impl_->error != presentation_error) {
        impl_->error = presentation_error;
        if (!impl_->error.isEmpty()) qCritical().noquote() << impl_->error;
        emit presentationErrorChanged();
    }
    if (impl_->error.isEmpty())
        impl_->recovery_attempts = 0;
    else
        scheduleRecovery();
}
void PresentationWindow::scheduleRecovery() {
    if (impl_->is_stopping || impl_->is_recovery_pending || impl_->recovery_attempts >= 3) return;
    impl_->is_recovery_pending = true;
    // 图形模块失败不阻塞通信/控制；间隔重建，连续三次失败后保留明确错误而不忙循环。
    QTimer::singleShot(1000, this, [this] {
        impl_->is_recovery_pending = false;
        if (impl_->is_stopping || impl_->error.isEmpty() || !isExposed()) return;
        ++impl_->recovery_attempts;
        QMetaObject::invokeMethod(
            impl_->presenter,
            [this] {
                impl_->presenter->shutdown();
                impl_->presenter->error.clear();
            },
            Qt::BlockingQueuedConnection);
        syncScene();
    });
}
QImage PresentationWindow::grabFrame() {
    if (!impl_->control) return grabWindow();
    syncScene();
    QImage image;
    QMetaObject::invokeMethod(
        impl_->presenter, [this, &image] { image = impl_->presenter->grab(); },
        Qt::BlockingQueuedConnection);
    return image;
}
void PresentationWindow::stopRendering() {
    if (!impl_->control || impl_->is_stopping) return;
    impl_->is_stopping = true;
    {
        QMutexLocker lock(&impl_->wake->mutex);
        impl_->wake->target = nullptr;
        impl_->wake->render = {};
    }
    QMetaObject::invokeMethod(
        impl_->presenter,
        [this] {
            impl_->presenter->shutdown();
            // Qt 的渲染上下文/动画控制器必须回 GUI，随后基类析构才能正确释放。
            impl_->control->prepareThread(thread());
        },
        Qt::BlockingQueuedConnection);
    impl_->thread.quit();
    impl_->thread.wait();
}
bool PresentationWindow::event(QEvent* event) {
    if (impl_ && impl_->control) {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed &&
            !impl_->is_stopping) {
            // 原生 surface 可因屏幕/窗口类型变化重建；保留工作线程，下一次 expose 重新初始化。
            QMetaObject::invokeMethod(
                impl_->presenter,
                [this] {
                    impl_->presenter->is_exposed = false;
                    impl_->presenter->shutdown();
                    impl_->presenter->error.clear();
                },
                Qt::BlockingQueuedConnection);
        }
        if (event->type() == QEvent::Expose && !impl_->is_stopping) {
            const bool is_exposed = isExposed();
            QMetaObject::invokeMethod(
                impl_->presenter, [this, is_exposed] { impl_->presenter->is_exposed = is_exposed; },
                Qt::BlockingQueuedConnection);
        }
        if (event->type() == QEvent::Expose || event->type() == QEvent::Resize ||
            event->type() == QEvent::DevicePixelRatioChange)
            scheduleSync();
    }
    return QQuickWindow::event(event);
}
