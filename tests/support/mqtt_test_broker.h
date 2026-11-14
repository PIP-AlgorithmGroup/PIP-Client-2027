#pragma once

#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <utility>

/** 测试线程独占的 MQTT 3.1.1 对端；只支持本机单连接夹具，不作为产品 broker。 */
class TestBroker : public QTcpServer {
   public:
    /** 只安装信号；调用方 listen 启动回环监听，QTcpServer 拥有收到的 socket。 */
    TestBroker() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            socket_ = nextPendingConnection();
            buffer_.clear();
            pending_tail_.clear();
            ++publication_generation_;
            ++connection_count;
            connect(socket_, &QTcpSocket::readyRead, this, [this] {
                buffer_ += socket_->readAll();
                readPackets();
            });
        });
    }

    /** 向已连接客户端注入 QoS0 包并模拟 TCP 分段；retained 用于验证旧消息拒绝。 */
    void publish(const QString& topic, const QByteArray& payload, bool retained = false) {
        const auto topic_bytes = topic.toUtf8();
        QByteArray body;
        body.append(char(topic_bytes.size() >> 8));
        body.append(char(topic_bytes.size() & 255));
        body += topic_bytes;
        body += payload;
        QByteArray packet(1, char(retained ? 0x31 : 0x30));
        auto remaining = body.size();
        do {
            auto digit = remaining % 128;
            remaining /= 128;
            packet.append(char(digit | (remaining ? 0x80 : 0)));
        } while (remaining);
        // 分两次写入以覆盖 TCP 分段，MQTT 包重组交给实际传输库。
        // Windows 定时器可能晚于下一次 publish；先完成旧包，不能把两条 MQTT 包交叉拼接。
        if (!pending_tail_.isEmpty()) socket_->write(std::exchange(pending_tail_, {}));
        socket_->write(packet + body.left(1));
        pending_tail_ = body.mid(1);
        const auto generation = ++publication_generation_;
        const QPointer<QTcpSocket> target = socket_;
        QTimer::singleShot(1, this, [this, target, generation] {
            if (target && generation == publication_generation_) {
                target->write(std::exchange(pending_tail_, {}));
            }
        });
    }

    /** 已连接时立即中断 TCP，模拟 Broker 或链路故障。 */
    void dropConnection() { socket_->abort(); }
    bool reject_subscription = false;                         ///< 使用 SUBACK 0x80 拒绝。
    bool acknowledge_subscription = true;                     ///< false 模拟 SUBACK 超时。
    bool send_initial_message = false;                        ///< SUBACK 后注入测试初始消息。
    bool acknowledge_publication = true;                      ///< false 模拟 QoS1 PUBACK 丢失。
    int connection_count = 0;                                 ///< 已接受 TCP 连接累计值。
    QStringList subscribed_topics;                            ///< 收到的订阅，保留跨重连记录。
    QList<QPair<QString, QByteArray>> received_publications;  ///< 客户端上行 Topic/原始负载。

   private:
    void readPackets() {
        while (buffer_.size() >= 2) {
            qsizetype length = 0;
            qsizetype position = 1;
            qsizetype multiplier = 1;
            unsigned char digit;
            do {
                if (position >= buffer_.size()) {
                    return;
                }
                digit = static_cast<unsigned char>(buffer_[position++]);
                length += (digit & 127) * multiplier;
                multiplier *= 128;
            } while (digit & 128);
            if (buffer_.size() - position < length) {
                return;
            }
            const auto header = static_cast<unsigned char>(buffer_[0]);
            const auto body = buffer_.mid(position, length);
            buffer_.remove(0, position + length);
            if ((header >> 4) == 1) {
                socket_->write(QByteArray::fromHex("20020000"));
            } else if ((header >> 4) == 8 && body.size() >= 5) {
                const auto topic_size = (static_cast<unsigned char>(body[2]) << 8) |
                                        static_cast<unsigned char>(body[3]);
                subscribed_topics.append(QString::fromUtf8(body.mid(4, topic_size)));
                if (acknowledge_subscription) {
                    QByteArray reply = QByteArray::fromHex("9003") + body.left(2);
                    reply.append(char(reject_subscription ? 0x80 : 1));
                    socket_->write(reply);
                    if (send_initial_message) {
                        // 在该 topic 的 SUBACK 后立即发初始状态，其余 topic 可能仍待确认。
                        const auto topic = body.mid(4, topic_size);
                        QByteArray message = QByteArray::fromHex("3100");
                        message[1] = char(2 + topic.size() + 2);
                        message.append(char(topic.size() >> 8));
                        message.append(char(topic.size() & 255));
                        message += topic;
                        message += QByteArray::fromHex("2804");
                        socket_->write(message);
                    }
                }
            } else if ((header >> 4) == 3 && body.size() >= 2) {
                const auto size = (quint8(body[0]) << 8) | quint8(body[1]);
                const auto qos = (header >> 1) & 3;
                const auto offset = 2 + size;
                received_publications.append(
                    {QString::fromUtf8(body.mid(2, size)), body.mid(offset + (qos ? 2 : 0))});
                if (qos == 1 && acknowledge_publication) {
                    socket_->write(QByteArray::fromHex("4002") + body.mid(offset, 2));
                }
            } else if ((header >> 4) == 12) {
                socket_->write(QByteArray::fromHex("d000"));
            }
        }
    }

    QPointer<QTcpSocket> socket_;
    QByteArray buffer_;
    QByteArray pending_tail_;
    quint64 publication_generation_ = 0;
};
