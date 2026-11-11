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
    /** 校验并复制一个 UDP 数据报；非法/未完成/过期返回空，不输出部分码流。 */
    QByteArray push(const QByteArray& datagram, qint64 now_ms);
    /** 丢弃超过 150 ms 的组帧；失效帧在当前串行编号窗口内不会复活。 */
    void expire(qint64 now_ms);
    /** 当前待重组帧数，仅用于资源预算检查。 */
    qsizetype pendingCount() const;

   private:
    struct Frame {
        quint32 size = 0;
        qint64 started_ms = 0;
        quint32 received_bytes = 0;
        QMap<int, QByteArray> parts;
    };
    void retire(quint16 id);
    int index_base_;
    QMap<quint16, Frame> frames_;
    QSet<quint16> retired_;
    bool has_output_ = false;
    quint16 last_output_ = 0;
};
