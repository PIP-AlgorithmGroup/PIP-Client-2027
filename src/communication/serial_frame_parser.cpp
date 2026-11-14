#include "serial_frame_parser.h"

#include <QtEndian>

namespace {
// 与官方查表算法等价的反射逐位实现：CRC8 init=0xff/poly=0x8c，
// CRC16 init=0xffff/poly=0x8408，无最终异或；避免依赖主机结构体布局。
quint8 crc8(QByteArrayView bytes) {
    quint8 crc = 0xff;
    for (const auto byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0x8c : 0);
    }
    return crc;
}

quint16 crc16(QByteArrayView bytes) {
    quint16 crc = 0xffff;
    for (const auto byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    return crc;
}
}  // namespace

QList<QByteArray> SerialFrameParser::feed(QByteArrayView bytes, qint64 now_ms) {
    QList<QByteArray> frames;
    if (!buffer_.isEmpty() && now_ms - partial_since_ms_ >= 100) {
        // 不把长期滞留的内部字节重新解释为新控制，丢弃后等待新输入重新同步。
        buffer_.clear();
        ++errors_;
    }
    // 逐字节推进使输入批次再大也不会扩大残帧缓存；校验失败只前进一个字节以恢复同步。
    for (const auto byte : bytes) {
        if (buffer_.isEmpty()) partial_since_ms_ = now_ms;
        buffer_.append(byte);
        while (!buffer_.isEmpty()) {
            if (quint8(buffer_[0]) != 0xa5) {
                buffer_.remove(0, 1);
                continue;
            }
            if (buffer_.size() < 5) break;
            const auto length = qFromLittleEndian<quint16>(buffer_.constData() + 1);
            if (crc8(QByteArrayView(buffer_).first(4)) != quint8(buffer_[4]) || length > 60) {
                buffer_.remove(0, 1);
                ++errors_;
                continue;
            }
            const auto frame_size = length + 9;
            if (buffer_.size() < frame_size) break;
            if (crc16(QByteArrayView(buffer_).first(frame_size - 2)) !=
                qFromLittleEndian<quint16>(buffer_.constData() + frame_size - 2)) {
                buffer_.remove(0, 1);
                ++errors_;
                continue;
            }
            if (qFromLittleEndian<quint16>(buffer_.constData() + 5) != 0x0311) {
                // 校验正确的其他命令必须整帧跳过，不能把它的负载当成嵌套控制帧。
                buffer_.remove(0, frame_size);
                ++errors_;
                continue;
            }
            const auto seq = quint8(buffer_[3]);
            const auto delta = quint8(seq - last_seq_);
            if (last_frame_ms_ >= 0 && now_ms - last_frame_ms_ < 100 &&
                (delta == 0 || delta > 127)) {
                ++discarded_sequence_;
            } else {
                frames.append(buffer_.mid(7, length));
                last_seq_ = seq;
                last_frame_ms_ = now_ms;
                ++accepted_;
            }
            buffer_.remove(0, frame_size);
        }
    }
    return frames;
}

void SerialFrameParser::reset() { *this = SerialFrameParser{}; }
void SerialFrameParser::resetStream() {
    buffer_.clear();
    last_frame_ms_ = -1;
}
quint64 SerialFrameParser::acceptedCount() const { return accepted_; }
quint64 SerialFrameParser::errorCount() const { return errors_; }
quint64 SerialFrameParser::discardedSequenceCount() const { return discarded_sequence_; }
