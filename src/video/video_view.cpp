#include "video_view.h"

#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <chrono>
#include <cmath>

#include "presentation_window.h"
#include "video_statistics.h"
#include "video_texture_node.h"

// 只在渲染线程回调与 GUI 定时读取间共享；不把 QML/纹理指针传入统计。
struct VideoPresentationStatistics {
    QMutex mutex;
    VideoStatistics statistics;
    VideoTimingStatistics timings;
    quint64 uploaded = 0;
    quint64 presented = 0;
    quint64 last_uploaded_revision = 0;
    bool has_frame = false;
    qint64 texture_ready_us = -1;
    qint64 assembled_us = -1;
    qint64 first_packet_us = -1;
    qint64 decoded_us = -1, picked_us = -1;
    int frame_id = -1;
    QString render_path;
    QString render_fallback;
};

namespace {
struct VideoNode : VideoTextureNode {
    quint64 revision = 0;
};
qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// GUI 同步只更新来源和几何。preprocess 在呈交线程直接取最新帧，绝不访问 QML/receiver。
struct IndependentVideoNode : VideoTextureNode {
    QQuickWindow* target_window = nullptr;
    std::shared_ptr<VideoFrameMailbox> mailbox;
    std::shared_ptr<VideoPresentationStatistics> state;
    QSizeF bounds;
    quint64 revision = 0;
    VideoFrame displayed;
    IndependentVideoNode() { setFlag(QSGNode::UsePreprocess); }
    void preprocess() override {
        const auto snapshot = mailbox ? mailbox->snapshot() : VideoFrameMailbox::Snapshot{};
        if (snapshot.revision != revision) {
            revision = snapshot.revision;
            displayed = snapshot.frame;
            const auto began = monotonicUs();
            const bool is_valid = !displayed.isNull() && setFrame(displayed, target_window);
            const auto ready = monotonicUs();
            const auto failure = fallbackReason();
            if (!is_valid) clearFrame();
            QMutexLocker lock(&state->mutex);
            if (is_valid != state->has_frame) {
                state->statistics.reset(ready);
                state->timings.reset();
            }
            state->has_frame = is_valid;
            state->render_path = is_valid ? path() : QString("unavailable");
            state->render_fallback = failure;
            if (is_valid) {
                ++state->uploaded;
                state->texture_ready_us = ready;
                state->assembled_us = displayed.assembled_us;
                state->first_packet_us = displayed.first_packet_us;
                state->decoded_us = displayed.decoded_us;
                state->picked_us = began;
                state->frame_id = displayed.frame_id;
                state->timings.record(VideoStage::Texture, ready - began, ready);
                state->timings.record(VideoStage::RenderDownload, downloadUs(), ready);
                state->timings.record(VideoStage::RenderConvert, convertUs(), ready);
                state->timings.record(VideoStage::GpuBridge, bridgeUs(), ready);
                if (displayed.decoded_us >= 0) {
                    state->timings.record(VideoStage::MailboxWait, began - displayed.decoded_us,
                                          ready);
                    state->timings.record(VideoStage::GuiWait, began - displayed.decoded_us, ready);
                }
            }
        }
        if (!displayed.isNull() && bounds.width() > 0 && bounds.height() > 0) {
            const double ratio =
                displayed.size().width() * displayed.pixel_aspect / displayed.size().height();
            if (std::isfinite(ratio) && ratio > 0) {
                const double width = qMin(bounds.width(), bounds.height() * ratio);
                const double height = width / ratio;
                setRect(
                    {(bounds.width() - width) / 2, (bounds.height() - height) / 2, width, height});
            }
        }
    }
};
}  // namespace

VideoView::VideoView(QQuickItem* parent)
    : QQuickItem(parent),
      presentation_statistics_(std::make_shared<VideoPresentationStatistics>()) {
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* window) {
        disconnect(swap_connection_);
        {
            QMutexLocker lock(&presentation_statistics_->mutex);
            presentation_statistics_->statistics.reset(monotonicUs());
            presentation_statistics_->timings.reset();
            presentation_statistics_->uploaded = presentation_statistics_->presented = 0;
            presentation_statistics_->last_uploaded_revision = 0;
        }
        if (!window) return;
        const auto state = presentation_statistics_;
        auto* independent_window = qobject_cast<PresentationWindow*>(window);
        swap_connection_ = connect(
            window, &QQuickWindow::frameSwapped, this,
            [state, independent_window] {
                QMutexLocker lock(&state->mutex);
                // UI 自身刷新或重复显示同一视频帧不计为新视频呈交。
                if (state->has_frame && state->uploaded != state->presented) {
                    const auto now_us = monotonicUs();
                    state->presented = state->uploaded;
                    state->statistics.completeFrame(static_cast<quint16>(state->presented), now_us);
                    if (state->texture_ready_us >= 0)
                        state->timings.record(VideoStage::PresentWait,
                                              now_us - state->texture_ready_us, now_us);
                    if (state->assembled_us >= 0)
                        state->timings.record(VideoStage::Local, now_us - state->assembled_us,
                                              now_us);
                    if (state->first_packet_us >= 0)
                        state->timings.record(VideoStage::ReceiveLocal,
                                              now_us - state->first_packet_us, now_us);
                    if (independent_window && independent_window->independentPresentation()) {
                        // 窗口停止并 join 渲染后才析构；此回调只发射值，不访问任何 GUI 项。
                        const auto id = state->frame_id;
                        const auto first = state->first_packet_us;
                        const auto decoded = state->decoded_us;
                        const auto picked = state->picked_us;
                        const auto ready = state->texture_ready_us;
                        lock.unlock();
                        emit independent_window->videoFramePresented(id, first, decoded, picked,
                                                                     ready, now_us);
                    }
                }
            },
            Qt::DirectConnection);
    });
    statistics_timer_.setInterval(250);
    connect(&statistics_timer_, &QTimer::timeout, this, [this] {
        double rate = -1;
        QVariantMap diagnostics;
        {
            QMutexLocker lock(&presentation_statistics_->mutex);
            if (presentation_statistics_->has_frame) {
                const auto snapshot = presentation_statistics_->statistics.snapshot(monotonicUs());
                rate = snapshot.value("receiveFps").toDouble();
                diagnostics = {{"low5Fps", snapshot.value("low5Fps")},
                               {"low1Fps", snapshot.value("low1Fps")},
                               {"lowSampleCount", snapshot.value("lowSampleCount")},
                               {"presentationFps", rate}};
                diagnostics["timings"] = presentation_statistics_->timings.snapshot(monotonicUs());
                auto* presenter = qobject_cast<PresentationWindow*>(window());
                diagnostics["presentationLoop"] =
                    presenter && presenter->independentPresentation() ? "independent-rhi" : "qt";
                diagnostics["renderPath"] = presentation_statistics_->render_path;
                diagnostics["renderFallback"] = presentation_statistics_->render_fallback;
            }
        }
        if (presentation_fps_ != rate || presentation_diagnostics_ != diagnostics) {
            presentation_fps_ = rate;
            presentation_diagnostics_ = std::move(diagnostics);
            emit presentationFpsChanged();
        }
    });
    statistics_timer_.start();
}
VideoReceiver* VideoView::receiver() const { return receiver_; }
double VideoView::presentationFps() const { return presentation_fps_; }
QVariantMap VideoView::presentationDiagnostics() const { return presentation_diagnostics_; }

void VideoView::setReceiver(VideoReceiver* receiver) {
    if (receiver_ == receiver) {
        return;
    }
    disconnect(frame_connection_);
    disconnect(destroy_connection_);
    receiver_ = receiver;
    {
        QMutexLocker lock(&presentation_statistics_->mutex);
        // 更换接收对象不沿用上一来源的间隔；refresh 在首个新来源画面到达时重新计时。
        presentation_statistics_->has_frame = false;
    }
    if (receiver_) {
        frame_connection_ =
            connect(receiver_, &VideoReceiver::frameChanged, this, &VideoView::refresh);
        destroy_connection_ = connect(receiver_, &QObject::destroyed, this, [this] {
            receiver_ = nullptr;
            refresh();
            emit receiverChanged();
        });
    }
    refresh();
    update();  // 来源即使同规格也必须同步邮箱身份，不能继续消费旧接收器。
    emit receiverChanged();
}

void VideoView::refresh() {
    const auto previous_rect = contentRect();
    frame_ = receiver_ ? receiver_->frame() : VideoFrame{};
    if (auto* presenter = qobject_cast<PresentationWindow*>(window());
        presenter && presenter->independentPresentation()) {
        // 视频已由解码线程唤醒呈交；同规格帧不再触发逐帧 QML/HUD 同步。
        if (previous_rect != contentRect()) {
            emit contentRectChanged();
            update();
        }
        return;
    }
    {
        QMutexLocker lock(&presentation_statistics_->mutex);
        const bool has_frame = !frame_.isNull();
        if (!has_frame || !presentation_statistics_->has_frame) {
            presentation_statistics_->statistics.reset(monotonicUs());
            presentation_statistics_->timings.reset();
            presentation_statistics_->presented = presentation_statistics_->uploaded;
        }
        presentation_statistics_->has_frame = has_frame;
    }
    if (frame_.isNull() && (presentation_fps_ != -1 || !presentation_diagnostics_.isEmpty())) {
        // 清屏与统计失效同一 GUI 事件生效，避免断流后短暂显示上一来源的 low。
        presentation_fps_ = -1;
        presentation_diagnostics_.clear();
        emit presentationFpsChanged();
    }
    ++revision_;
    emit contentRectChanged();
    // 信号处理也属于 GUI 准备；Qt 同步阶段 GUI 被阻塞，渲染线程可安全读取此 revision 的值。
    update_requested_us_ = monotonicUs();
    update();
}

QRectF VideoView::contentRect() const {
    if (frame_.isNull() || width() <= 0 || height() <= 0) {
        return {};
    }
    const double ratio = frame_.size().width() * frame_.pixel_aspect / frame_.size().height();
    if (!std::isfinite(ratio) || ratio <= 0) {
        return {};
    }
    const double fitted_width = qMin(width(), height() * ratio);
    const double fitted_height = fitted_width / ratio;
    return {(width() - fitted_width) / 2, (height() - fitted_height) / 2, fitted_width,
            fitted_height};
}

QSGNode* VideoView::updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) {
    if (auto* presenter = qobject_cast<PresentationWindow*>(window());
        presenter && presenter->independentPresentation()) {
        auto* node = static_cast<IndependentVideoNode*>(old_node);
        if (!node) node = new IndependentVideoNode;
        const auto mailbox = receiver_ ? receiver_->renderMailbox() : nullptr;
        if (node->mailbox != mailbox) {
            node->clearFrame();
            node->displayed = {};
            node->revision = 0;
            node->mailbox = mailbox;
        }
        node->target_window = window();
        node->state = presentation_statistics_;
        node->bounds = size();
        return node;
    }
    auto* node = static_cast<VideoNode*>(old_node);
    if (frame_.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new VideoNode;
    }
    if (node->revision != revision_) {
        const auto began_us = monotonicUs();
        if (!node->setFrame(frame_, window())) {
            QMutexLocker lock(&presentation_statistics_->mutex);
            presentation_statistics_->render_path = "unavailable";
            presentation_statistics_->render_fallback = node->fallbackReason();
            delete node;
            return nullptr;
        }
        node->revision = revision_;
        const auto ready_us = monotonicUs();
        QMutexLocker lock(&presentation_statistics_->mutex);
        // 场景图节点重建可能重新上传同一帧；这种资源恢复不算新视频帧。
        if (presentation_statistics_->last_uploaded_revision != revision_) {
            presentation_statistics_->render_path = node->path();
            presentation_statistics_->render_fallback = node->fallbackReason();
            presentation_statistics_->timings.record(VideoStage::RenderDownload, node->downloadUs(),
                                                     ready_us);
            presentation_statistics_->timings.record(VideoStage::RenderConvert, node->convertUs(),
                                                     ready_us);
            presentation_statistics_->timings.record(VideoStage::GpuBridge, node->bridgeUs(),
                                                     ready_us);
            presentation_statistics_->last_uploaded_revision = revision_;
            ++presentation_statistics_->uploaded;
            // texture 包含 CPU 平面准备、原生互操作及回退；Qt/RHI 可能延迟执行，不能称 GPU
            // 上传耗时。
            presentation_statistics_->timings.record(VideoStage::Texture, ready_us - began_us,
                                                     ready_us);
            if (frame_.decoded_us >= 0)
                presentation_statistics_->timings.record(VideoStage::GuiWait,
                                                         began_us - frame_.decoded_us, ready_us);
            // 只观测真正进入场景图的新帧，未显示而被邮箱/GUI 合并的帧不伪造渲染时间。
            if (frame_.decoded_us >= 0 && frame_.gui_received_us >= frame_.decoded_us &&
                update_requested_us_ >= frame_.gui_received_us) {
                presentation_statistics_->timings.record(
                    VideoStage::MailboxWait, frame_.gui_received_us - frame_.decoded_us, ready_us);
                presentation_statistics_->timings.record(
                    VideoStage::GuiPrepare, update_requested_us_ - frame_.gui_received_us,
                    ready_us);
                presentation_statistics_->timings.record(VideoStage::RenderWait,
                                                         began_us - update_requested_us_, ready_us);
            }
            presentation_statistics_->texture_ready_us = ready_us;
            presentation_statistics_->assembled_us = frame_.assembled_us;
            presentation_statistics_->first_packet_us = frame_.first_packet_us;
        }
    }
    node->setRect(contentRect());
    return node;
}

void VideoView::geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) {
    QQuickItem::geometryChange(new_geometry, old_geometry);
    emit contentRectChanged();
    update();
}
