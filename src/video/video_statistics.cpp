#include "video_statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

void VideoTimingStatistics::heartbeat(qint64 now_us) {
    if (last_heartbeat_us_ >= 0 && now_us >= last_heartbeat_us_)
        record(VideoStage::GuiHeartbeat,
               qMax(qint64(0), now_us - last_heartbeat_us_ - kHeartbeatIntervalMs * 1000), now_us);
    last_heartbeat_us_ = now_us;
}

void VideoTimingStatistics::record(VideoStage stage, qint64 duration_us, qint64 now_us) {
    if (duration_us < 0 || stage == VideoStage::Count) return;
    auto& samples = stages_[static_cast<size_t>(stage)];
    while (!samples.empty() && samples.front().ended_us <= now_us - 1000000) samples.pop_front();
    if (samples.size() == 512) samples.pop_front();
    samples.push_back({now_us, duration_us});
}

void VideoTimingStatistics::reset() {
    for (auto& samples : stages_) samples.clear();
    last_heartbeat_us_ = -1;
}

QVariantMap VideoTimingStatistics::snapshot(qint64 now_us) {
    static constexpr const char* names[] = {
        "codec",        "download",    "convert",    "queue",      "guiWait",
        "texture",      "presentWait", "local",      "assembly",   "decodeTotal",
        "receiveLocal", "mailboxWait", "guiPrepare", "renderWait", "guiHeartbeat"};
    QVariantMap result;
    for (size_t i = 0; i < stages_.size(); ++i) {
        auto& samples = stages_[i];
        while (!samples.empty() && samples.front().ended_us <= now_us - 1000000)
            samples.pop_front();
        std::vector<qint64> sorted;
        sorted.reserve(samples.size());
        for (const auto& sample : samples) sorted.push_back(sample.duration_us);
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double fraction) {
            return sorted.empty()
                       ? -1.0
                       : sorted[static_cast<size_t>(std::ceil(sorted.size() * fraction)) - 1] /
                             1000.0;
        };
        result[names[i]] = QVariantMap{
            {"samples", int(sorted.size())},
            {"meanMs", sorted.empty() ? -1.0
                                      : std::accumulate(sorted.begin(), sorted.end(), 0.0) /
                                            sorted.size() / 1000},
            {"maxMs", percentile(1)},
            {"p95Ms", percentile(.95)},
            {"p99Ms", percentile(.99)}};
    }
    return result;
}

VideoStatistics::VideoStatistics(qint64 origin_us) { reset(origin_us); }

void VideoStatistics::reset(qint64 origin_us) {
    origin_us_ = origin_us;
    last_frame_us_ = -1;
    last_id_ = 0;
    invalid_ = duplicates_ = incomplete_ = 0;
    buckets_.clear();
    intervals_.clear();
}

VideoStatistics::Bucket& VideoStatistics::bucket(qint64 now_us) {
    const auto time = now_us / 10000 * 10000;
    while (!buckets_.empty() && buckets_.front().time_us <= time - 1000000) buckets_.pop_front();
    if (buckets_.empty() || buckets_.back().time_us != time) {
        Bucket entry;
        entry.time_us = time;
        buckets_.push_back(entry);
    }
    return buckets_.back();
}

void VideoStatistics::datagram(int bytes, qint64 now_us) {
    auto& entry = bucket(now_us);
    entry.bytes += qMax(0, bytes);
    ++entry.packets;
}

void VideoStatistics::completeFrame(quint16 id, qint64 now_us) {
    auto& entry = bucket(now_us);
    ++entry.complete;
    if (last_frame_us_ >= 0) {
        const auto distance = static_cast<quint16>(id - last_id_);
        // 仅向前半区间可判断缺口；重复/倒退或来源跳变不推测缺失数量。
        if (distance > 0 && distance < 32768) {
            entry.missing += distance - 1;
            entry.expected += distance;
        }
        if (now_us > last_frame_us_) {
            intervals_.push_back({now_us, now_us - last_frame_us_});
            if (intervals_.size() > 4096) intervals_.pop_front();
        }
    }
    last_id_ = id;
    last_frame_us_ = now_us;
}

void VideoStatistics::decodeFinished(qint64 duration_us, bool produced, qint64 now_us) {
    auto& entry = bucket(now_us);
    ++entry.calls;
    entry.decode_us += qMax(qint64(0), duration_us);
    entry.decode_max_us = qMax(entry.decode_max_us, duration_us);
    if (produced) ++entry.decoded;
}

void VideoStatistics::assemblyCounters(quint64 invalid, quint64 duplicates, quint64 incomplete,
                                       qint64 now_us) {
    auto& entry = bucket(now_us);
    entry.invalid += invalid >= invalid_ ? invalid - invalid_ : 0;
    entry.duplicates += duplicates >= duplicates_ ? duplicates - duplicates_ : 0;
    entry.incomplete += incomplete >= incomplete_ ? incomplete - incomplete_ : 0;
    invalid_ = invalid;
    duplicates_ = duplicates;
    incomplete_ = incomplete;
}

QVariantMap VideoStatistics::snapshot(qint64 now_us) {
    bucket(now_us);
    while (!intervals_.empty() && intervals_.front().ended_us <= now_us - 10000000)
        intervals_.pop_front();
    Bucket sum;
    for (const auto& entry : buckets_) {
        sum.bytes += entry.bytes;
        sum.packets += entry.packets;
        sum.complete += entry.complete;
        sum.decoded += entry.decoded;
        sum.missing += entry.missing;
        sum.expected += entry.expected;
        sum.invalid += entry.invalid;
        sum.duplicates += entry.duplicates;
        sum.incomplete += entry.incomplete;
        sum.calls += entry.calls;
        sum.decode_us += entry.decode_us;
        sum.decode_max_us = qMax(sum.decode_max_us, entry.decode_max_us);
    }
    std::vector<qint64> durations;
    durations.reserve(intervals_.size() + 1);
    for (const auto& interval : intervals_) durations.push_back(interval.duration_us);
    // 停流中的未结束间隔也参与 low，防止画面已停而仍展示历史 60fps low。
    if (last_frame_us_ >= 0 && now_us > last_frame_us_ && now_us - last_frame_us_ < 10000000 &&
        !intervals_.empty())
        durations.push_back(now_us - last_frame_us_);
    std::sort(durations.begin(), durations.end(), std::greater<qint64>());
    const auto low = [&](double fraction) {
        if (intervals_.size() < static_cast<size_t>(std::ceil(1 / fraction))) return -1.0;
        const auto count = static_cast<size_t>(std::ceil(durations.size() * fraction));
        const auto total = std::accumulate(durations.begin(), durations.begin() + count, 0.0);
        return total > 0 ? 1000000.0 * count / total : -1.0;
    };
    // 至少观察 1 秒后才报告滚动速率；不能用启动时短窗口外推为稳定吞吐。
    const bool ready = now_us - origin_us_ >= 1000000;
    return {{"receiveFps", ready ? double(sum.complete) : -1.0},
            {"decodeFps", ready ? double(sum.decoded) : -1.0},
            {"low5Fps", low(0.05)},
            {"low1Fps", low(0.01)},
            {"lowSampleCount", int(intervals_.size())},
            {"decodeMs", sum.calls ? double(sum.decode_us) / sum.calls / 1000.0 : -1.0},
            {"decodeMaxMs", sum.calls ? double(sum.decode_max_us) / 1000.0 : -1.0},
            {"megabitsPerSecond", ready ? sum.bytes * 8.0 / 1000000 : -1.0},
            {"packetsPerSecond", ready ? double(sum.packets) : -1.0},
            {"frameLossPercent", sum.expected ? 100.0 * sum.missing / sum.expected : -1.0},
            {"packetLossPercent", -1.0},
            {"invalidPackets", QVariant::fromValue(sum.invalid)},
            {"duplicatePackets", QVariant::fromValue(sum.duplicates)},
            {"incompleteFrames", QVariant::fromValue(sum.incomplete)}};
}
