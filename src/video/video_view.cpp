#include "video_view.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <cmath>

namespace {
struct VideoNode : QSGSimpleTextureNode {
    quint64 revision = 0;
};
}  // namespace

VideoView::VideoView(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }
VideoReceiver* VideoView::receiver() const { return receiver_; }

void VideoView::setReceiver(VideoReceiver* receiver) {
    if (receiver_ == receiver) {
        return;
    }
    disconnect(frame_connection_);
    disconnect(destroy_connection_);
    receiver_ = receiver;
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
    emit receiverChanged();
}

void VideoView::refresh() {
    frame_ = receiver_ ? receiver_->frame() : VideoFrame{};
    ++revision_;
    emit contentRectChanged();
    update();
}

QRectF VideoView::contentRect() const {
    if (frame_.image.isNull() || width() <= 0 || height() <= 0) {
        return {};
    }
    const double ratio = frame_.image.width() * frame_.pixel_aspect / frame_.image.height();
    if (!std::isfinite(ratio) || ratio <= 0) {
        return {};
    }
    const double fitted_width = qMin(width(), height() * ratio);
    const double fitted_height = fitted_width / ratio;
    return {(width() - fitted_width) / 2, (height() - fitted_height) / 2, fitted_width,
            fitted_height};
}

QSGNode* VideoView::updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) {
    auto* node = static_cast<VideoNode*>(old_node);
    if (frame_.image.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new VideoNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    if (node->revision != revision_) {
        auto* texture = window()->createTextureFromImage(frame_.image);
        if (!texture) {
            delete node;
            return nullptr;
        }
        // ownsTexture=true 时 Qt 在 setTexture 内释放上一张纹理，不能再次手工 delete。
        node->setTexture(texture);
        node->revision = revision_;
    }
    node->setRect(contentRect());
    return node;
}

void VideoView::geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) {
    QQuickItem::geometryChange(new_geometry, old_geometry);
    emit contentRectChanged();
    update();
}
