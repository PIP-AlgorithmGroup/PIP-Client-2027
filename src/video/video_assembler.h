#pragma once

#include <QByteArray>
#include <QMap>
#include <QSet>

/**
 * 单来源官方 UDP 重组器；仅调用线程访问。push 的时间为单调毫秒。
 * 工程上限：帧 2 MiB、4 帧在途、150 ms 组帧；只输出完整且比上次输出更新的帧。
 * 官方未指定分片起始编号，调用者必须显式传入 0 或 1；支持可变长度分片。
 * 每帧最多 4096 个分片是资源保护策略，不是官方协议默认值。
 */
class VideoAssembler {
   public:
    /** index_base 必须为 0/1，生命周期限定在一个 UDP 来源会话。 */
    explicit VideoAssembler(int index_base);
    /** 校验并复制数据报；now_ms 为超时时钟，packet_us 为应用读包的稳态微秒，未知为 -1。
     * 非法/未完成/过期返回空；成功时 firstPacketUs 返回该帧首个有效分片时刻。
     */
    QByteArray push(const QByteArray& datagram, qint64 now_ms, qint64 packet_us = -1);
    /** 最近一次 push 成功输出帧的首包稳态微秒；未出帧/未知为 -1，同线程读取。 */
    qint64 firstPacketUs() const;
    /** 丢弃超过 150 ms 的组帧；失效帧在当前串行编号窗口内不会复活。 */
    void expire(qint64 now_ms);
    /** 当前待重组帧数，仅用于资源预算检查。 */
    qsizetype pendingCount() const;
    /** 当前来源累计本地拒绝/重复/未完成帧计数；不等于网络丢包数量。 */
    struct Counters {
        quint64 invalid = 0;     ///< 非法包或同编号负载冲突。
        quint64 duplicates = 0;  ///< 在途帧相同分片重发；已输出旧帧不重复计数。
        quint64 incomplete = 0;  ///< 超时、预算淘汰或被更新帧超越的在途帧。
    };
    /** 只读副本；与 push/expire 同线程调用，不重置计数。 */
    Counters counters() const;

   private:
    struct Frame {
        quint32 size = 0;
        qint64 started_ms = 0;
        qint64 first_packet_us = -1;
        quint32 received_bytes = 0;
        QMap<int, QByteArray> parts;
    };
    void retire(quint16 id);
    int index_base_;
    QMap<quint16, Frame> frames_;
    QSet<quint16> retired_;
    bool has_output_ = false;
    quint16 last_output_ = 0;
    Counters counters_;
    qint64 first_packet_us_ = -1;  ///< 只描述本次输出；失效/未完成不得沿用上一帧起点。
};
