#pragma once

#include <QVariantMap>
#include <deque>

/** 单线程视频统计；调用方传同一单调时基的微秒，不使用媒体 PTS 或墙钟。
 * 1 秒速率按 10ms 桶计数；low 使用最近 10 秒最多 4096 个完整收帧间隔。
 * 不可测/预热值为 -1。跨线程只能转交 snapshot 的值副本，不共享本对象。
 */
class VideoStatistics {
   public:
    /** origin_us 为当前来源开始时刻；不延续上一来源的编号或采样。 */
    explicit VideoStatistics(qint64 origin_us = 0);
    /** 清除全部统计，开始新的来源或监听会话。 */
    void reset(qint64 origin_us);
    /** 记录当前来源收到的数据报字节数（含协议头，不含 IP/UDP 头）。 */
    void datagram(int bytes, qint64 now_us);
    /** 按重组器输出顺序记录完整帧；16 位序号向前缺口包含发送端跳帧等原因。 */
    void completeFrame(quint16 id, qint64 now_us);
    /** 记录一次解码调用耗时（含 RGBA 转换），produced 表示输出有效图像。 */
    void decodeFinished(qint64 duration_us, bool produced, qint64 now_us);
    /** 接收重组器累计计数，在当前来源内不回退；转换为最近 1 秒增量。 */
    void assemblyCounters(quint64 invalid, quint64 duplicates, quint64 incomplete, qint64 now_us);
    /** 返回可复制的只读统计值；会裁剪已过期数据，建议每 250ms 调用一次。
     * receive/decodeFps 为最近 1 秒计数，low 为最慢 ceil(N*p) 间隔的均值倒数；
     * 5%/1% 至少需 20/100 个已完成间隔，停流中的未结束间隔也参与排序。
     * frameLossPercent 是前进编号缺口/编号跨度，不能作为 UDP 包损或链路丢包归因。
     */
    QVariantMap snapshot(qint64 now_us);

   private:
    struct Bucket {
        qint64 time_us = 0;
        quint64 bytes = 0, packets = 0, complete = 0, decoded = 0, missing = 0, expected = 0;
        quint64 invalid = 0, duplicates = 0, incomplete = 0, calls = 0;
        qint64 decode_us = 0;
    };
    struct Interval {
        qint64 ended_us;
        qint64 duration_us;
    };
    Bucket& bucket(qint64 now_us);
    qint64 origin_us_ = 0;
    qint64 last_frame_us_ = -1;
    quint16 last_id_ = 0;
    quint64 invalid_ = 0, duplicates_ = 0, incomplete_ = 0;
    std::deque<Bucket> buckets_;
    std::deque<Interval> intervals_;
};
