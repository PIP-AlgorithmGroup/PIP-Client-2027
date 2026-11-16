#pragma once

#include <QVariantMap>
#include <array>
#include <deque>

/** 互不混用的本机耗时阶段；均为稳态时钟观测，不是 GPU 时间戳或采集端延迟。 */
enum class VideoStage {
    Codec,
    Download,
    Convert,
    Queue,
    GuiWait,  ///< 解码→取帧；qt 循环为 GUI/同步等待，独立循环为呈交线程邮箱等待。
    Texture,
    PresentWait,
    Local,
    Assembly,        ///< 进入解码的 AU：首包读出至完整组帧；不含 socket 缓冲等待。
    DecodeTotal,     ///< 单次 decode 同步调用的整体计时，不能由分阶段 P95 相加得到。
    ReceiveLocal,    ///< 同一呈现帧的首包读出至 Qt 呈交；不是显示器亮屏时间。
    MailboxWait,     ///< 解码完成→消费者取帧；消费者由 presentationLoop 区分 GUI/独立呈交线程。
    GuiPrepare,      ///< GUI 取帧至 VideoView 请求刷新，包含 GUI 信号处理。
    RenderWait,      ///< 请求刷新至该帧 updatePaintNode 开始，包含场景图调度。
    GuiHeartbeat,    ///< GUI 心跳实际间隔超出目标间隔的部分，不等于某个函数的执行时间。
    RenderDownload,  ///< 显示回退中的硬件同步/下载；包含在 texture 内，不重复相加。
    RenderConvert,   ///< 显示回退中的 CPU RGBA 转色；包含在 texture 内。
    GpuBridge,       ///< 原生 GPU 共享/复制的 CPU 提交及同步，不是 GPU 时间。
    Count
};

/** 单线程分阶段耗时统计；各阶段最近 1 秒最多保留 512 个样本，跨线程只传值快照。 */
class VideoTimingStatistics {
   public:
    /// GUI 心跳目标周期，毫秒；使用 PreciseTimer，统计超时部分而非整个周期。
    static constexpr int kHeartbeatIntervalMs = 20;
    /** 在所属线程记录心跳；now_us 为同一单调微秒时基，首个心跳只建立基准。 */
    void heartbeat(qint64 now_us);
    /** 忽略负耗时（未执行阶段）；now_us 和 duration_us 均为单调时钟微秒。 */
    void record(VideoStage stage, qint64 duration_us, qint64 now_us);
    /** 清除来源/会话的全部阶段，不能延续上代样本。 */
    void reset();
    /** 返回各阶段 samples、meanMs/maxMs/p95Ms/p99Ms，空阶段耗时为 -1；最近秩分位数。 */
    QVariantMap snapshot(qint64 now_us);

   private:
    struct Sample {
        qint64 ended_us;
        qint64 duration_us;
    };
    std::array<std::deque<Sample>, static_cast<size_t>(VideoStage::Count)> stages_;
    qint64 last_heartbeat_us_ = -1;  ///< reset 后重新预热，不能将重置前的空闲误计为停顿。
};

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
    /** 记录一次解码调用耗时（正常通路至帧引用就绪），produced 表示本代有效画面。
     * snapshot 的 decodeMs / decodeMaxMs 分别为最近 1 秒均值和最大值，单位毫秒。
     */
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
        qint64 decode_us = 0, decode_max_us = 0;
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
