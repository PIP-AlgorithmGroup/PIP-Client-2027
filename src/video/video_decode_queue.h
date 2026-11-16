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
        qint64 enqueued_us;      ///< 实际入队的稳态微秒，用于计算待处理年龄。
    };
    /** 消费线程当前同步操作；Idle 不代表线程已得到调度，不能用于推断 CPU 忙闲。 */
    enum class Operation { Idle, Initialize, Decode, Reset };
    /** 最近一次 decode 调用各阶段耗时，单位微秒；-1 表示阶段未执行/未知。
     * 与解码器逐次调用统计同义，不是 GPU 执行时间，也不是滑动窗口均值。
     */
    struct DecodeCosts {
        qint64 total_us = -1;
        qint64 codec_us = -1;
        qint64 download_us = -1;
        qint64 convert_us = -1;
    };
    /** 短锁入队；received_us 为完整组帧时刻，first_packet_us 为首包读出时刻，均为稳态微秒。
     * 首包未知可传 -1；frame_id 为协议编号，未知 -1，仅用于诊断。
     * 停止/非法长度返回 false，溢出清空旧队列保留当前 AU。
     * enqueued_us 为入队稳态微秒，-1 使用锁内时钟；batch_id 为接收循环批号，0 表示未知。
     * 显式时钟只用于确定性测试，必须与其他时间戳同源且按调用次序单调递增。
     */
    bool push(const QByteArray& bytes, qint64 received_us, qint64 first_packet_us = -1,
              int frame_id = -1, qint64 enqueued_us = -1, quint64 batch_id = 0);
    /** 等待最多 10ms；空闲或停止返回空，由调用方定期检查停止与诊断时钟。 */
    std::optional<Unit> take();
    /** 清空待解码数据并使在途结果失效；reason 为固定诊断标识，帧编号未知传 -1。
     * 返回新代数；上下文与代数在同一临界区发布，不记录原始视频负载。
     * now_us 为同源稳态微秒，-1 使用锁内时钟；显式值供确定性测试。
     */
    quint64 reset(const QString& reason = "explicit_reset", int frame_id = -1,
                  int previous_frame_id = -1, qint64 now_us = -1);
    /** 消费线程在同步操作前发布状态；时间为稳态微秒，初始化/重置的 frame_id 为 -1。
     * 仅短暂持队列锁，不持锁进入驱动；唯一消费者须配对调用 endOperation。
     */
    void beginOperation(Operation operation, quint64 epoch, int frame_id, qint64 started_us);
    /** 同步操作返回后立即发布空闲；仅 Decode 更新最近一次耗时（包括无输出或旧代调用）。
     * ended_us 为返回时的稳态微秒；costs 与该次调用一致。线程安全，不改变恢复快照。
     */
    void endOperation(qint64 ended_us, DecodeCosts costs);
    /** 最近一次重置的原因、代数、清空前深度、丢弃计数与帧编号，加当前队列深度。
     * 包含清空前固定的工作状态、最近 decode 和最多 8 次入队节奏；无样本的耗时为 -1。
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
    void resetLocked(const QString& reason, int frame_id, int previous_frame_id, qint64 now_us);
    // 只保留最近 8 次成功入队的轻量元数据，不保留额外 AU 负载；受 mutex_ 保护。
    struct Enqueue {
        qint64 at_us;
        int frame_id;
        quint64 batch_id;
    };
    mutable QMutex mutex_;
    QWaitCondition ready_;
    std::deque<Unit> units_;
    quint64 epoch_ = 0;
    quint64 dropped_ = 0;
    bool stopped_ = false;
    QVariantMap reset_context_;  ///< 只保存最新重置，受 mutex_ 保护；事件历史由接收器有界保存。
    std::deque<Enqueue> recent_enqueues_;
    quint64 enqueue_batch_id_ = 0;  ///< 同批已接收 AU 数包含触发溢出的 AU；0 批号不统计。
    int enqueue_batch_units_ = 0;
    qint64 enqueue_batch_started_us_ = -1;
    qint64 enqueue_batch_last_us_ = -1;
    Operation operation_ = Operation::Idle;  ///< 以下工作状态均受 mutex_ 保护，由消费线程发布。
    quint64 operation_epoch_ = 0;
    int operation_frame_id_ = -1;
    qint64 operation_started_us_ = -1;
    DecodeCosts last_decode_costs_;
    qint64 last_decode_ended_us_ = -1;
    quint64 last_decode_epoch_ = 0;  ///< 最近解码允许属于旧代；事件中显式标记，绝不伪装为新代。
    int last_decode_frame_id_ = -1;
};
