#include "video_decode_queue.h"

#include <QMutexLocker>
#include <chrono>

namespace {
qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
double milliseconds(qint64 microseconds) { return microseconds < 0 ? -1.0 : microseconds / 1000.0; }
}  // namespace

bool VideoDecodeQueue::push(const QByteArray& bytes, qint64 received_us, qint64 first_packet_us,
                            int frame_id, qint64 enqueued_us, quint64 batch_id) {
    if (bytes.isEmpty() || bytes.size() > 2 * 1024 * 1024) return false;
    QMutexLocker lock(&mutex_);
    if (stopped_) return false;
    if (enqueued_us < 0) enqueued_us = monotonicUs();
    if (batch_id == 0 || batch_id != enqueue_batch_id_) {
        enqueue_batch_id_ = batch_id;
        enqueue_batch_units_ = 0;
        enqueue_batch_started_us_ = enqueued_us;
    }
    if (batch_id != 0) ++enqueue_batch_units_;
    enqueue_batch_last_us_ = enqueued_us;
    // 包含触发溢出的这次入队，才能看到造成满队列的最后一个间隔；历史与队列深度无关。
    if (recent_enqueues_.size() == 8) recent_enqueues_.pop_front();
    recent_enqueues_.push_back({enqueued_us, frame_id, batch_id});
    if (units_.size() == 3) {
        resetLocked("queue_overflow", frame_id, units_.back().frame_id, enqueued_us);
    }
    units_.push_back({bytes, received_us, epoch_, first_packet_us, frame_id, enqueued_us});
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

void VideoDecodeQueue::beginOperation(Operation operation, quint64 epoch, int frame_id,
                                      qint64 started_us) {
    QMutexLocker lock(&mutex_);
    operation_ = operation;
    operation_epoch_ = epoch;
    operation_frame_id_ = frame_id;
    operation_started_us_ = started_us;
}

void VideoDecodeQueue::endOperation(qint64 ended_us, DecodeCosts costs) {
    QMutexLocker lock(&mutex_);
    if (operation_ == Operation::Decode) {
        last_decode_costs_ = costs;
        last_decode_ended_us_ = ended_us;
        last_decode_epoch_ = operation_epoch_;
        last_decode_frame_id_ = operation_frame_id_;
    }
    operation_ = Operation::Idle;
    operation_started_us_ = -1;
    operation_frame_id_ = -1;
}

void VideoDecodeQueue::resetLocked(const QString& reason, int frame_id, int previous_frame_id,
                                   qint64 now_us) {
    const auto depth = units_.size();
    // 所有异常证据在同一短锁内、清队列前固定；窗口统计清空和旧代解码返回都不能改写它。
    const auto oldest_wait_us = units_.empty() ? -1 : now_us - units_.front().enqueued_us;
    const int oldest_frame_id = units_.empty() ? -1 : units_.front().frame_id;
    QVariantList intervals;
    QVariantList frame_ids;
    QVariantList batch_ids;
    qint64 previous_us = -1;
    for (const auto& enqueue : recent_enqueues_) {
        if (previous_us >= 0) intervals.append(milliseconds(enqueue.at_us - previous_us));
        previous_us = enqueue.at_us;
        frame_ids.append(enqueue.frame_id);
        batch_ids.append(QVariant::fromValue(enqueue.batch_id));
    }
    const auto operation_name = [this] {
        switch (operation_) {
            case Operation::Initialize:
                return "initialize";
            case Operation::Decode:
                return "decode";
            case Operation::Reset:
                return "reset";
            case Operation::Idle:
                return "idle";
        }
        return "idle";
    };
    dropped_ += units_.size();
    units_.clear();
    ++epoch_;
    reset_context_ = {
        {"reason", reason},
        {"epoch", QVariant::fromValue(epoch_)},
        {"queueDepthBefore", int(depth)},
        {"droppedByReset", QVariant::fromValue(quint64(depth))},
        {"droppedTotal", QVariant::fromValue(dropped_)},
        {"frameId", frame_id},
        {"previousFrameId", previous_frame_id},
        {"evidenceUs", now_us},
        {"oldestQueuedFrameId", oldest_frame_id},
        {"oldestQueueWaitMs", milliseconds(oldest_wait_us)},
        {"workerOperation", operation_name()},
        {"workerEpoch", QVariant::fromValue(operation_epoch_)},
        {"workerFrameId", operation_frame_id_},
        {"workerElapsedMs",
         milliseconds(operation_started_us_ < 0 ? -1 : now_us - operation_started_us_)},
        {"lastDecodeEpoch", QVariant::fromValue(last_decode_epoch_)},
        {"lastDecodeFrameId", last_decode_frame_id_},
        {"lastDecodeAgeMs",
         milliseconds(last_decode_ended_us_ < 0 ? -1 : now_us - last_decode_ended_us_)},
        {"lastDecodeTotalMs", milliseconds(last_decode_costs_.total_us)},
        {"lastDecodeCodecMs", milliseconds(last_decode_costs_.codec_us)},
        {"lastDecodeDownloadMs", milliseconds(last_decode_costs_.download_us)},
        {"lastDecodeConvertMs", milliseconds(last_decode_costs_.convert_us)},
        {"recentEnqueueLastUs", previous_us},
        {"enqueueBatchId", QVariant::fromValue(enqueue_batch_id_)},
        {"enqueueBatchUnits", enqueue_batch_units_},
        {"enqueueBatchSpanMs",
         milliseconds(enqueue_batch_id_ == 0 ? -1
                                             : enqueue_batch_last_us_ - enqueue_batch_started_us_)},
        {"recentEnqueueIntervalsMs", intervals},
        {"recentEnqueueFrameIds", frame_ids},
        {"recentEnqueueBatchIds", batch_ids}};
    // 溢出后的连续过载仍需观察前因；主动断链/换来源后重新建立节奏历史。
    if (reason != "queue_overflow") recent_enqueues_.clear();
}

quint64 VideoDecodeQueue::reset(const QString& reason, int frame_id, int previous_frame_id,
                                qint64 now_us) {
    QMutexLocker lock(&mutex_);
    resetLocked(reason, frame_id, previous_frame_id, now_us < 0 ? monotonicUs() : now_us);
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
    resetLocked("session_stop", -1, -1, monotonicUs());
    ready_.wakeOne();
}
