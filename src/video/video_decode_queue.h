#pragma once

#include <QByteArray>
#include <QMutex>
#include <QWaitCondition>
#include <deque>
#include <optional>

/** 收包→解码单生产/单消费交接；不包含 FFmpeg 对象，可在两线程并发调用。
 * 最多 3 个待处理 AU，每个不超过 2MiB。不等待解码，不作为平滑播放缓存。
 * 溢出、来源变化递增代数，消费方必须重置参考链，旧代已在途结果也需拒绝。
 * 销毁前由持有方 stop 并 join 消费线程；队列自身不拥有或结束线程。
 */
class VideoDecodeQueue {
   public:
    /** 完整 AU 的不可变共享副本；时间是 steady_clock 单调微秒，代数由队列分配。 */
    struct Unit {
        QByteArray bytes;
        qint64 received_us;
        quint64 epoch;
        qint64 first_packet_us;  ///< 本 AU 的首个有效分片读出时刻，未知为 -1。
    };
    /** 短锁入队；received_us 为完整组帧时刻，first_packet_us 为首包读出时刻，均为稳态微秒。
     * 首包未知可传 -1；停止/非法长度返回 false，溢出清空旧队列保留当前 AU。
     */
    bool push(const QByteArray& bytes, qint64 received_us, qint64 first_packet_us = -1);
    /** 等待最多 10ms；空闲或停止返回空，由调用方定期检查停止与诊断时钟。 */
    std::optional<Unit> take();
    /** 清空待解码数据并使在途结果失效；返回新的代数。 */
    quint64 reset();
    /** 只读代数；调用方发布解码结果前核对，防止来源变化后回放旧画面。 */
    quint64 epoch() const;
    /** 本接收会话累计的本地待解码丢弃数量，包含重置和队列溢出；不是网络丢帧。 */
    quint64 dropped() const;
    /** 停止并唤醒消费者；幂等，不再接受新数据。 */
    void stop();

   private:
    mutable QMutex mutex_;
    QWaitCondition ready_;
    std::deque<Unit> units_;
    quint64 epoch_ = 0;
    quint64 dropped_ = 0;
    bool stopped_ = false;
};
