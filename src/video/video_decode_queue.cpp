#include "video_decode_queue.h"

#include <QMutexLocker>

bool VideoDecodeQueue::push(const QByteArray& bytes, qint64 received_us, qint64 first_packet_us,
                            int frame_id) {
    if (bytes.isEmpty() || bytes.size() > 2 * 1024 * 1024) return false;
    QMutexLocker lock(&mutex_);
    if (stopped_) return false;
    if (units_.size() == 3) {
        resetLocked("queue_overflow", frame_id, units_.back().frame_id);
    }
    units_.push_back({bytes, received_us, epoch_, first_packet_us, frame_id});
    ready_.wakeOne();
    return true;
}

std::optional<VideoDecodeQueue::Unit> VideoDecodeQueue::take() {
    QMutexLocker lock(&mutex_);
    if (units_.empty() && !stopped_) ready_.wait(&mutex_, 10);
    if (stopped_ || units_.empty()) return std::nullopt;
    auto unit = std::move(units_.front());
    units_.pop_front();
    return unit;
}

void VideoDecodeQueue::resetLocked(const QString& reason, int frame_id, int previous_frame_id) {
    const auto depth = units_.size();
    dropped_ += units_.size();
    units_.clear();
    ++epoch_;
    reset_context_ = {{"reason", reason},
                      {"epoch", QVariant::fromValue(epoch_)},
                      {"queueDepthBefore", int(depth)},
                      {"droppedByReset", QVariant::fromValue(quint64(depth))},
                      {"droppedTotal", QVariant::fromValue(dropped_)},
                      {"frameId", frame_id},
                      {"previousFrameId", previous_frame_id}};
}

quint64 VideoDecodeQueue::reset(const QString& reason, int frame_id, int previous_frame_id) {
    QMutexLocker lock(&mutex_);
    resetLocked(reason, frame_id, previous_frame_id);
    return epoch_;
}

QVariantMap VideoDecodeQueue::resetContext() const {
    QMutexLocker lock(&mutex_);
    auto context = reset_context_;
    if (context.isEmpty()) {
        context = {{"reason", ""},
                   {"epoch", QVariant::fromValue(epoch_)},
                   {"queueDepthBefore", 0},
                   {"droppedByReset", 0},
                   {"droppedTotal", QVariant::fromValue(dropped_)},
                   {"frameId", -1},
                   {"previousFrameId", -1}};
    }
    context["queueDepth"] = int(units_.size());
    return context;
}

quint64 VideoDecodeQueue::epoch() const {
    QMutexLocker lock(&mutex_);
    return epoch_;
}
quint64 VideoDecodeQueue::dropped() const {
    QMutexLocker lock(&mutex_);
    return dropped_;
}
void VideoDecodeQueue::stop() {
    QMutexLocker lock(&mutex_);
    if (stopped_) return;
    stopped_ = true;
    resetLocked("session_stop", -1, -1);
    ready_.wakeOne();
}
