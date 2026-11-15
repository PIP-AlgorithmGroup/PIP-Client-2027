#pragma once

#include <QByteArray>
#include <QMutex>
#include <QVariantMap>
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
        int frame_id;            ///< 16 位协议帧编号，未知为 -1，仅用于诊断关联。
    };
    /** 短锁入队；received_us 为完整组帧时刻，first_packet_us 为首包读出时刻，均为稳态微秒。
     * 首包未知可传 -1；frame_id 为协议编号，未知 -1，仅用于诊断。
     * 停止/非法长度返回 false，溢出清空旧队列保留当前 AU。
     */
    bool push(const QByteArray& bytes, qint64 received_us, qint64 first_packet_us = -1,
              int frame_id = -1);
    /** 等待最多 10ms；空闲或停止返回空，由调用方定期检查停止与诊断时钟。 */
    std::optional<Unit> take();
    /** 清空待解码数据并使在途结果失效；reason 为固定诊断标识，帧编号未知传 -1。
     * 返回新代数；上下文与代数在同一临界区发布，不记录原始视频负载。
     */
    quint64 reset(const QString& reason = "explicit_reset", int frame_id = -1,
                  int previous_frame_id = -1);
    /** 最近一次重置的原因、代数、清空前深度、丢弃计数与帧编号，加当前队列深度。
     * 线程安全值副本；消费队列不会改变最近重置的上下文，重置前返回空原因。
     */
    QVariantMap resetContext() const;
    /** 只读代数；调用方发布解码结果前核对，防止来源变化后回放旧画面。 */
    quint64 epoch() const;
    /** 本接收会话累计的本地待解码丢弃数量，包含重置、溢出与停止清空；不是网络丢帧。 */
    quint64 dropped() const;
    /** 停止并唤醒消费者；幂等，不再接受新数据。 */
    void stop();

   private:
    // 调用方持 mutex_；记录的是清空前状态，避免消费线程取帧后丢失溢出证据。
    void resetLocked(const QString& reason, int frame_id, int previous_frame_id);
    mutable QMutex mutex_;
    QWaitCondition ready_;
    std::deque<Unit> units_;
    quint64 epoch_ = 0;
    quint64 dropped_ = 0;
    bool stopped_ = false;
    QVariantMap reset_context_;  ///< 只保存最新重置，受 mutex_ 保护；事件历史由接收器有界保存。
};
