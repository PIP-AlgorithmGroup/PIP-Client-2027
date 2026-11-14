#pragma once

#include <QFile>
#include <QHostAddress>
#include <QList>
#include <QUdpSocket>
#include <QtEndian>

namespace video_test {
/** 仅供测量输入：按 HEVC AUD(type=35) 切分 Annex B，不推断缺少 AUD 的访问单元边界。
 * 要求首个 AUD 从字节 0 开始，支持 3/4 字节起始码；不作为产品协议解析器。
 */
inline QList<QByteArray> audUnits(const QByteArray& stream) {
    QList<qsizetype> starts;
    for (qsizetype i = 0; i + 5 < stream.size(); ++i) {
        int prefix = 0;
        if (stream[i] == 0 && stream[i + 1] == 0) {
            if (stream[i + 2] == 1)
                prefix = 3;
            else if (stream[i + 2] == 0 && stream[i + 3] == 1)
                prefix = 4;
        }
        if (!prefix) continue;
        if (((static_cast<unsigned char>(stream[i + prefix]) >> 1) & 63) == 35) starts.append(i);
        i += prefix;
    }
    if (starts.isEmpty() || starts.front() != 0) return {};
    QList<QByteArray> units;
    for (qsizetype i = 0; i < starts.size(); ++i) {
        const auto end = i + 1 < starts.size() ? starts[i + 1] : stream.size();
        units.append(stream.mid(starts[i], end - starts[i]));
    }
    return units;
}

/** 读取项目自产测试图案的单个 Annex B IDR 访问单元，失败返回空。 */
inline QByteArray fixture() {
    QFile file(PIP_VIDEO_FIXTURE);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

/** 临时绑定本机端口；返回 0 表示 OS 拒绝。释放与接收器绑定间的竞争由测试状态检查捕捉。 */
inline quint16 freePort() {
    QUdpSocket socket;
    return socket.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)) ? socket.localPort() : 0;
}

/** 按官方头封包，通过真实 UDP 发送；reverse 模拟乱序，skip 模拟丢片。 */
inline bool sendFrame(QUdpSocket& socket, quint16 port, quint16 frame, int base = 0,
                      bool reverse = false, int skip = -1) {
    const auto unit = fixture();
    const int count = (unit.size() + 1391) / 1392;
    if (unit.isEmpty()) {
        return false;
    }
    for (int part = 0; part < count; ++part) {
        const int index = reverse ? count - part - 1 : part;
        if (index == skip) {
            continue;
        }
        QByteArray packet(8, '\0');
        qToBigEndian(frame, packet.data());
        qToBigEndian(static_cast<quint16>(index + base), packet.data() + 2);
        qToBigEndian(static_cast<quint32>(unit.size()), packet.data() + 4);
        packet.append(unit.mid(index * 1392, 1392));
        if (socket.writeDatagram(packet, QHostAddress::LocalHost, port) != packet.size()) {
            return false;
        }
    }
    return true;
}
}  // namespace video_test
