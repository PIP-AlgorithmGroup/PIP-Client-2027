#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

/**
 * 单线程增量解析官方图传帧，仅接受小端 0x0311 和 0..60 字节数据，不解释负载。
 * CRC 采用 2027 V1.0.0 附录一；缓存最多 69 字节。时间为调用者单调时钟毫秒。
 * 工程约束：残帧 100 ms 到期；连续有效帧间隔不足 100 ms 时丢弃重复/逆序 seq，允许回绕。
 */
class SerialFrameParser {
   public:
    /** 追加任意分片并返回本批有效负载；空输入可驱动残帧超时，不进行 I/O。 */
    QList<QByteArray> feed(QByteArrayView bytes, qint64 now_ms);
    /** 新设备/连接调用，清空残帧、序号和全部统计；不允许与 feed 并发。 */
    void reset();
    /** 身份/转发代次改变时清除残帧和序号窗口，累计统计保持不变；仅所属线程调用。 */
    void resetStream();
    /** 以下计数只在解析器所属线程读取，生命周期内累计，reset 清零。 */
    quint64 acceptedCount() const;
    quint64 errorCount() const;
    quint64 discardedSequenceCount() const;

   private:
    QByteArray buffer_;  ///< 最多一帧；不持有外部输入视图。
    qint64 partial_since_ms_ = 0;
    qint64 last_frame_ms_ = -1;
    quint8 last_seq_ = 0;
    quint64 accepted_ = 0;
    quint64 errors_ = 0;
    quint64 discarded_sequence_ = 0;
};
