#include "video_assembler.h"

#include <QtEndian>
#include <algorithm>

namespace {
constexpr quint32 kMaxFrame = 2 * 1024 * 1024;
// RFC 1982 半区间比较；官方 16 位帧号回绕后仍可判断新旧。
bool newer(quint16 candidate, quint16 previous) {
    const auto distance = static_cast<quint16>(candidate - previous);
    return distance != 0 && distance < 32768;
}
}  // namespace

VideoAssembler::VideoAssembler(int index_base) : index_base_(index_base) {
    Q_ASSERT(index_base == 0 || index_base == 1);
}

void VideoAssembler::retire(quint16 id) {
    frames_.remove(id);
    retired_.insert(id);
    // 只保留当前编号附近的拒绝记录，避免长时间运行时增长或阻碍完整回绕。
    if (retired_.size() > 256) {
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (newer(id, *it) && static_cast<quint16>(id - *it) > 128) {
                it = retired_.erase(it);
            } else {
                ++it;
            }
        }
        if (retired_.size() > 256) {
            retired_.clear();
            retired_.insert(id);
        }
    }
}

void VideoAssembler::expire(qint64 now_ms) {
    const auto ids = frames_.keys();
    for (const auto id : ids) {
        if (now_ms - frames_[id].started_ms > 150) {
            retire(id);
        }
    }
}

QByteArray VideoAssembler::push(const QByteArray& datagram, qint64 now_ms) {
    expire(now_ms);
    if (datagram.size() <= 8 || datagram.size() > 1400) {
        return {};
    }
    const auto id = qFromBigEndian<quint16>(datagram.constData());
    const int index = qFromBigEndian<quint16>(datagram.constData() + 2) - index_base_;
    const auto size = qFromBigEndian<quint32>(datagram.constData() + 4);
    if (size == 0 || size > kMaxFrame || index < 0 || index >= 4096 ||
        static_cast<quint32>(index) >= size || datagram.size() - 8 > size ||
        retired_.contains(id) || (has_output_ && !newer(id, last_output_))) {
        return {};
    }
    if (!frames_.contains(id)) {
        if (frames_.size() == 4) {
            auto oldest = std::min_element(
                frames_.begin(), frames_.end(),
                [](const auto& a, const auto& b) { return a.started_ms < b.started_ms; });
            retire(oldest.key());
        }
        frames_.insert(id, Frame{size, now_ms, 0, {}});
    }
    auto& frame = frames_[id];
    const auto payload = datagram.mid(8);
    if (frame.size != size || (frame.parts.contains(index) && frame.parts[index] != payload)) {
        retire(id);
        return {};
    }
    if (frame.parts.contains(index)) {
        return {};
    }
    frame.received_bytes += payload.size();
    if (frame.received_bytes > size) {
        retire(id);
        return {};
    }
    frame.parts.insert(index, payload);
    if (frame.received_bytes != size) {
        return {};
    }
    // 大小相等但序号不连续仍为不完整帧，绝不能把缺片数据交给解码器。
    if (frame.parts.firstKey() != 0 || frame.parts.lastKey() != frame.parts.size() - 1) {
        retire(id);
        return {};
    }
    QByteArray output;
    output.reserve(size);
    for (const auto& part : frame.parts) {
        output.append(part);
    }
    last_output_ = id;
    has_output_ = true;
    retire(id);
    const auto ids = frames_.keys();
    for (const auto pending : ids) {
        if (!newer(pending, id)) {
            retire(pending);
        }
    }
    return output;
}

qsizetype VideoAssembler::pendingCount() const { return frames_.size(); }
