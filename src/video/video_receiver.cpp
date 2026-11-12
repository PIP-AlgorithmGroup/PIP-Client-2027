#include "video_receiver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QMutexLocker>
#include <QNetworkDatagram>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUdpSocket>
#include <QtEndian>

#include "video_assembler.h"
#include "video_statistics.h"

namespace {
bool validConfiguration(const QString& address, int port, int index_base) {
    QHostAddress host;
    return host.setAddress(address) && port >= 1 && port <= 65535 &&
           (index_base == 0 || index_base == 1);
}
}  // namespace

VideoReceiver::VideoReceiver(const QString& config_path, QObject* parent)
    : QObject(parent),
      config_path_(config_path.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                             "/video.ini"
                       : config_path) {
    QSettings settings(config_path_, QSettings::IniFormat);
    bool port_ok = false;
    bool base_ok = false;
    const auto address = settings.value("address", address_).toString();
    const auto port = settings.value("port", port_).toInt(&port_ok);
    const auto base = settings.value("index_base", index_base_).toInt(&base_ok);
    config_valid_ = settings.status() == QSettings::NoError && port_ok && base_ok &&
                    validConfiguration(address, port, base);
    if (config_valid_) {
        address_ = address;
        port_ = port;
        index_base_ = base;
    } else {
        error_ = "图传配置无效，请重新保存监听配置";
    }
}

VideoReceiver::~VideoReceiver() { stop(); }
QString VideoReceiver::bindAddress() const { return address_; }
int VideoReceiver::port() const { return port_; }
int VideoReceiver::indexBase() const { return index_base_; }
bool VideoReceiver::running() const { return running_; }
QString VideoReceiver::status() const { return status_; }
QString VideoReceiver::error() const { return error_; }
QString VideoReceiver::sourceAddress() const { return source_address_; }
int VideoReceiver::sourcePort() const { return source_port_; }
VideoFrame VideoReceiver::frame() const { return frame_; }
QVariantMap VideoReceiver::diagnostics() const { return diagnostics_; }

bool VideoReceiver::configure(const QString& address, int port, int index_base) {
    if (!validConfiguration(address.trimmed(), port, index_base)) {
        configuration_error_ = "请输入数字 IP、1–65535 端口以及 0/1 分片起始序号";
        error_ = configuration_error_;
        emit statusChanged();
        return false;
    }
    // 整组原子替换而非合并损坏的旧 INI；保存失败不触碰正在运行的 socket。
    QSaveFile settings(config_path_);
    const auto contents = QString("[General]\naddress=%1\nport=%2\nindex_base=%3\n")
                              .arg(address.trimmed())
                              .arg(port)
                              .arg(index_base)
                              .toUtf8();
    if (!QDir().mkpath(QFileInfo(config_path_).absolutePath()) ||
        !settings.open(QIODevice::WriteOnly) || settings.write(contents) != contents.size() ||
        !settings.commit()) {
        configuration_error_ = "无法保存图传配置，原会话保持不变";
        error_ = configuration_error_;
        emit statusChanged();
        return false;
    }
    const bool was_running = running_;
    stop();
    address_ = address.trimmed();
    port_ = port;
    index_base_ = index_base;
    config_valid_ = true;
    configuration_error_.clear();
    error_.clear();
    emit configurationChanged();
    emit statusChanged();
    if (was_running) {
        start();
    }
    return true;
}

void VideoReceiver::start() {
    if (running_) {
        return;
    }
    if (!config_valid_) {
        emit statusChanged();
        return;
    }
    stop();
    {
        QMutexLocker lock(&mutex_);
        pending_frame_ = {};
        pending_error_.clear();
        pending_status_ = "等待图传";
        failed_ = false;
    }
    running_ = true;
    configuration_error_.clear();
    error_.clear();
    status_ = "正在监听";
    worker_.reset(
        QThread::create([this, address = address_, port = port_, base = index_base_,
                         generation = generation_] { receive(address, port, base, generation); }));
    worker_->start();
    emit statusChanged();
}

void VideoReceiver::stop() {
    ++generation_;
    if (worker_) {
        worker_->requestInterruption();
        worker_->wait();
        worker_.reset();
    }
    running_ = false;
    status_ = "已停止";
    frame_ = {};
    {
        QMutexLocker lock(&mutex_);
        pending_frame_ = {};
        pending_source_address_.clear();
        pending_source_port_ = 0;
        displayed_revision_ = revision_;
        pending_diagnostics_.clear();
        poll_pending_ = false;
    }
    diagnostics_.clear();
    emit diagnosticsChanged();
    if (!source_address_.isEmpty() || source_port_ != 0) {
        source_address_.clear();
        source_port_ = 0;
        emit sourceChanged();
    }
    emit frameChanged();
    emit statusChanged();
}

void VideoReceiver::requestPollLocked(quint64 generation) {
    if (poll_pending_) return;
    poll_pending_ = true;
    // this 为队列调用的 QObject 上下文；析构自动取消调用，代数隔离 stop/start。
    QMetaObject::invokeMethod(
        this,
        [this, generation] {
            if (generation == generation_ && running_) poll();
        },
        Qt::QueuedConnection);
}

void VideoReceiver::receive(const QString& address, int port, int index_base, quint64 generation) {
    QUdpSocket socket;
    if (!socket.bind(QHostAddress(address), port, QUdpSocket::DontShareAddress)) {
        QMutexLocker lock(&mutex_);
        pending_error_ = "UDP 监听失败：" + socket.errorString();
        failed_ = true;
        requestPollLocked(generation);
        return;
    }
    // 约 8 MiB 系统接收缓冲；实际容量由操作系统限制，用户态不追加无限队列。
    socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024);
    // 默认自动选择；部署诊断可显式指定 FFmpeg 后端/设备，错误配置仍可降级 CPU。
    // 不写入网络配置，也不增加比赛设置中的协议字段。
    auto decoder = std::make_unique<HevcDecoder>(qEnvironmentVariable("PIP_VIDEO_DECODER", "auto"),
                                                 qEnvironmentVariable("PIP_VIDEO_DEVICE"));
    if (!decoder->error().isEmpty()) {
        QMutexLocker lock(&mutex_);
        pending_error_ = decoder->error();
        failed_ = true;
        requestPollLocked(generation);
        return;
    }
    VideoAssembler assembler(index_base);
    QElapsedTimer clock;
    clock.start();
    VideoStatistics statistics;
    qint64 next_statistics_us = 0;
    QHostAddress source;
    quint16 source_port = 0;
    qint64 last_packet_ms = -1;
    qint64 last_decoded_ms = -1;
    bool has_unit = false;
    quint16 last_unit = 0;
    while (!QThread::currentThread()->isInterruptionRequested()) {
        if (!socket.hasPendingDatagrams()) {
            socket.waitForReadyRead(10);
        }
        const auto now = clock.elapsed();
        assembler.expire(now);
        const auto timestamp_us = clock.nsecsElapsed() / 1000;
        if (timestamp_us >= next_statistics_us) {
            const auto counters = assembler.counters();
            statistics.assemblyCounters(counters.invalid, counters.duplicates, counters.incomplete,
                                        timestamp_us);
            auto snapshot = statistics.snapshot(timestamp_us);
            snapshot["decoder"] = decoder->backend();
            snapshot["decoderDevice"] = decoder->device();
            snapshot["hardwareActive"] = decoder->hardwareActive();
            snapshot["decoderFallback"] = decoder->fallbackReason();
            QMutexLocker lock(&mutex_);
            pending_diagnostics_ = std::move(snapshot);
            requestPollLocked(generation);
            next_statistics_us = timestamp_us + 250000;
        }
        // 来源断流 1 秒后解除锁定，并丢掉解码参考帧；不混入其他发送端的分片。
        if (last_packet_ms >= 0 && now - last_packet_ms > 1000) {
            source.clear();
            assembler = VideoAssembler(index_base);
            statistics.reset(clock.nsecsElapsed() / 1000);
            next_statistics_us = 0;
            decoder->reset();
            last_packet_ms = -1;
            has_unit = false;
            // 来源状态和图像邮箱分离；即使没有成功解码，也不能一直展示过期来源。
            QMutexLocker lock(&mutex_);
            pending_source_address_.clear();
            pending_source_port_ = 0;
            requestPollLocked(generation);
        }
        if (last_decoded_ms >= 0 && now - last_decoded_ms > 1000) {
            QMutexLocker lock(&mutex_);
            pending_frame_ = {};
            ++revision_;
            pending_status_ = "图传中断，等待画面";
            last_decoded_ms = -1;
            requestPollLocked(generation);
        }
        // 每轮有界处理，洪泛时也能及时观察停止与超时，不在 GUI 线程排逐包信号。
        for (int count = 0; count < 128 && socket.hasPendingDatagrams() &&
                            !QThread::currentThread()->isInterruptionRequested();
             ++count) {
            const auto packet = socket.receiveDatagram(1401);
            // 统计仅含当前锁定来源；未知来源首个格式有效包在下面锁定时纳入。
            const bool from_source = !source.isNull() && packet.senderAddress() == source &&
                                     packet.senderPort() == source_port;
            if (from_source) statistics.datagram(packet.data().size(), clock.nsecsElapsed() / 1000);
            if (packet.data().size() <= 8 || packet.data().size() > 1400) {
                if (from_source) assembler.push(packet.data(), clock.elapsed());
                continue;
            }
            if (!source.isNull() &&
                (packet.senderAddress() != source || packet.senderPort() != source_port)) {
                continue;
            }
            // 第一个格式上可能有效的数据报锁定来源；停止/超时后允许发送端重启。
            const auto unit = assembler.push(packet.data(), clock.elapsed());
            if (!unit.isEmpty() || assembler.pendingCount() > 0) {
                if (source.isNull()) {
                    statistics.datagram(packet.data().size(), clock.nsecsElapsed() / 1000);
                    // 只在首次锁定时跨线程发布端点；避免逐包字符串分配和 GUI 事件堆积。
                    QMutexLocker lock(&mutex_);
                    pending_source_address_ = packet.senderAddress().toString();
                    pending_source_port_ = packet.senderPort();
                    requestPollLocked(generation);
                }
                source = packet.senderAddress();
                source_port = packet.senderPort();
                last_packet_ms = clock.elapsed();
            }
            if (unit.isEmpty()) {
                continue;
            }
            const auto unit_id = qFromBigEndian<quint16>(packet.data().constData());
            statistics.completeFrame(unit_id, clock.nsecsElapsed() / 1000);
            if (has_unit && static_cast<quint16>(unit_id - last_unit) != 1) {
                // 丢弃旧参考帧，保留设备和失败历史；网络抖动不能触发昂贵的 GPU 重新探测。
                decoder->reset();
                QMutexLocker lock(&mutex_);
                pending_frame_ = {};
                ++revision_;
                pending_status_ = "等待关键帧";
                requestPollLocked(generation);
            }
            has_unit = true;
            last_unit = unit_id;
            const auto decode_started = clock.nsecsElapsed();
            auto decoded = decoder->decode(unit);
            const auto decoded_at = clock.nsecsElapsed();
            statistics.decodeFinished((decoded_at - decode_started) / 1000, !decoded.image.isNull(),
                                      decoded_at / 1000);
            QMutexLocker lock(&mutex_);
            if (!decoder->error().isEmpty()) {
                pending_error_ = decoder->error();
            }
            if (!decoded.image.isNull()) {
                pending_frame_ = std::move(decoded);
                ++revision_;
                last_decoded_ms = clock.elapsed();
                pending_status_ = "正在接收";
                pending_error_.clear();
            }
            requestPollLocked(generation);
        }
    }
}

void VideoReceiver::poll() {
    bool has_frame = false;
    bool has_failure = false;
    QString status;
    QString error;
    QString source_address;
    int source_port = 0;
    QVariantMap diagnostics;
    {
        QMutexLocker lock(&mutex_);
        // 先释放合并标志，再复制同锁邮箱；复制之后的新帧会再安排一次唤醒。
        poll_pending_ = false;
        if (revision_ != displayed_revision_) {
            frame_ = pending_frame_;
            displayed_revision_ = revision_;
            has_frame = true;
        }
        status = pending_status_;
        error = configuration_error_.isEmpty() ? pending_error_ : configuration_error_;
        has_failure = failed_;
        source_address = pending_source_address_;
        source_port = pending_source_port_;
        diagnostics = pending_diagnostics_;
    }
    if (has_failure) {
        stop();
        status = "接收失败";
    }
    if (!has_failure && diagnostics_ != diagnostics) {
        diagnostics_ = std::move(diagnostics);
        emit diagnosticsChanged();
    }
    if (!has_failure && (source_address_ != source_address || source_port_ != source_port)) {
        source_address_ = source_address;
        source_port_ = source_port;
        emit sourceChanged();
    }
    if (has_frame && !has_failure) {
        emit frameChanged();
    }
    if (status_ != status || error_ != error) {
        status_ = status;
        error_ = error;
        emit statusChanged();
    }
}
