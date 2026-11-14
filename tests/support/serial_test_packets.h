#pragma once

#include <QByteArray>
#include <QtEndian>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#endif

namespace serial_test {
/** 测试帧构造器；产品 CRC 的独立固定向量另由 serial_frame_parser_test 验证。 */
inline QByteArray frame(const QByteArray& payload, quint8 seq) {
    QByteArray result(7, char(0));
    result[0] = char(0xa5);
    qToLittleEndian<quint16>(quint16(payload.size()), result.data() + 1);
    result[3] = char(seq);
    quint8 header_crc = 255;
    for (int index = 0; index < 4; ++index) {
        header_crc ^= quint8(result[index]);
        for (int bit = 0; bit < 8; ++bit)
            header_crc = (header_crc >> 1) ^ ((header_crc & 1) ? 0x8c : 0);
    }
    result[4] = char(header_crc);
    result[5] = char(0x11);
    result[6] = char(0x03);
    result += payload;
    quint16 crc = 65535;
    for (const auto byte : result) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    result.append(char(crc & 255));
    result.append(char(crc >> 8));
    return result;
}

#ifdef Q_OS_LINUX
/** Linux 真实内核 PTY，走产品 QSerialPort；不替换产品解析/收发代码，析构释放 master。 */
class PseudoTerminal {
   public:
    PseudoTerminal() {
        descriptor_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (descriptor_ >= 0 && grantpt(descriptor_) == 0 && unlockpt(descriptor_) == 0)
            path_ = QString::fromLocal8Bit(ptsname(descriptor_));
    }
    ~PseudoTerminal() { closeMaster(); }
    /** 返回从端设备路径；空值表示内核 PTY 创建失败。 */
    QString path() const { return path_; }
    /** 小批次非阻塞写入，返回实际字节数；测试必须检查是否完整写入。 */
    qint64 send(const QByteArray& bytes) {
        return ::write(descriptor_, bytes.data(), bytes.size());
    }
    /** 模拟控制器拔出；可重复调用。 */
    void closeMaster() {
        if (descriptor_ >= 0) ::close(descriptor_);
        descriptor_ = -1;
    }

   private:
    int descriptor_ = -1;
    QString path_;
};
#endif
}  // namespace serial_test
