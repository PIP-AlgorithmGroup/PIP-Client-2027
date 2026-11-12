#include "video_decode_queue.h"

#include <QMutexLocker>

bool VideoDecodeQueue::push(const QByteArray& bytes, qint64 received_us) {
    if (bytes.isEmpty() || bytes.size() > 2 * 1024 * 1024) return false;
    QMutexLocker lock(&mutex_);
    if (stopped_) return false;
    if (units_.size() == 3) {
        dropped_ += units_.size();
        units_.clear();
        ++epoch_;
    }
    units_.push_back({bytes, received_us, epoch_});
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

quint64 VideoDecodeQueue::reset() {
    QMutexLocker lock(&mutex_);
    dropped_ += units_.size();
    units_.clear();
    return ++epoch_;
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
    units_.clear();
    ++epoch_;
    ready_.wakeOne();
}
