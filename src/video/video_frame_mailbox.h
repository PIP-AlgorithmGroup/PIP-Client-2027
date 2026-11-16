#pragma once

#include <QMutex>
#include <QMutexLocker>

#include "hevc_decoder.h"

/** 独立于 QObject/GUI 的单槽最新帧邮箱；可跨线程发布、读取和清屏。
 * 快照持有不可变像素引用；消费者跳过旧 revision，不排逐帧队列。
 */
class VideoFrameMailbox {
   public:
    /** 同锁取得的画面与递增序号；空画面同样有 revision，用于可靠清屏。 */
    struct Snapshot {
        VideoFrame frame;
        quint64 revision = 0;
    };
    /** 替换单槽；旧 surface 在邮箱锁外释放，允许发布空帧。 */
    void publish(VideoFrame frame) {
        {
            QMutexLocker lock(&mutex_);
            std::swap(frame, latest_.frame);
            ++latest_.revision;
        }
    }
    /** 只复制引用与序号，不下载像素或等待 GUI；返回副本在后续清屏/析构后仍有效。 */
    Snapshot snapshot() const {
        QMutexLocker lock(&mutex_);
        return latest_;
    }

   private:
    mutable QMutex mutex_;
    Snapshot latest_;
};
