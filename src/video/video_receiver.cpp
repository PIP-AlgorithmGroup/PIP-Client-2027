#include "video_receiver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QMutexLocker>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUdpSocket>
#include <QtEndian>
#include <algorithm>
#include <chrono>
#include <thread>

#include "video_assembler.h"
#include "video_decode_queue.h"
#include "video_statistics.h"

namespace {
bool validDecoderChoice(const QString& choice) {
    static const QRegularExpression key(
        "\\A(auto|cpu|d3d11va:[0-9a-f]{1,16}|cuda:[0-9a-f]{32}|vaapi:[A-Za-z0-9_.:-]{1,128})\\z");
    return key.match(choice).hasMatch();
}
bool validConfiguration(const QString& address, int port, int index_base) {
    QHostAddress host;
    return host.setAddress(address) && port >= 1 && port <= 65535 &&
           (index_base == 0 || index_base == 1);
}
qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
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
    const auto choice = settings.value("decoder", "auto").toString();
    config_valid_ = settings.status() == QSettings::NoError && port_ok && base_ok &&
                    validConfiguration(address, port, base) && validDecoderChoice(choice);
    if (config_valid_) {
        address_ = address;
        port_ = port;
        index_base_ = base;
        decoder_choice_ = choice;
    } else {
        error_ = "图传配置无效，请重新保存监听配置";
    }
    decoder_devices_ = {
        QVariantMap{{"key", "auto"}, {"label", "自动选择"}, {"available", true}},
        QVariantMap{{"key", "cpu"}, {"label", "CPU · 软件解码"}, {"available", true}}};
    refreshDecoderDevices();
}

VideoReceiver::~VideoReceiver() {
    stop();
    // queued 回调绑定 this，析构取消未处理回调；先 join 保证驱动线程不再引用本对象。
    if (probe_thread_) probe_thread_->wait();
}
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
QVariantMap VideoReceiver::takeRecoveryEvents() {
    QMutexLocker lock(&mutex_);
    QVariantMap batch{{"events", recovery_events_},
                      {"omitted", QVariant::fromValue(recovery_events_omitted_)},
                      {"observedUs", monotonicUs()}};
    recovery_events_.clear();
    recovery_events_omitted_ = 0;
    return batch;
}

void VideoReceiver::recordRecoveryLocked(const QString& event, QVariantMap fields,
                                         quint64 generation) {
    fields["event"] = event;
    fields["atUs"] = monotonicUs();
    fields["generation"] = QVariant::fromValue(generation);
    if (recovery_events_.size() == 32) {
        recovery_events_.removeFirst();
        ++recovery_events_omitted_;
    }
    recovery_events_.append(fields);
}
QString VideoReceiver::decoderChoice() const { return decoder_choice_; }
QVariantList VideoReceiver::decoderDevices() const { return decoder_devices_; }
bool VideoReceiver::probingDevices() const { return probing_devices_; }

void VideoReceiver::refreshDecoderDevices() {
    if (probing_devices_) return;
    if (probe_thread_) probe_thread_->wait();
    probing_devices_ = true;
    emit decoderDevicesChanged();
    probe_thread_.reset(QThread::create([this] {
        auto devices = HevcDecoder::probeDevices();
        devices.prepend(QVariantMap{{"key", "auto"},
                                    {"label", "自动选择"},
                                    {"available", true},
                                    {"reason", "按平台设备依次尝试，最终回退 CPU"}});
        QMetaObject::invokeMethod(
            this,
            [this, devices = std::move(devices)] {
                decoder_devices_ = devices;
                const bool found =
                    std::any_of(devices.begin(), devices.end(), [this](const auto& entry) {
                        return entry.toMap().value("key").toString() == decoder_choice_;
                    });
                if (!found)
                    decoder_devices_.append(
                        QVariantMap{{"key", decoder_choice_},
                                    {"label", "已保存设备不可用 · 将回退 CPU"},
                                    {"available", false},
                                    {"reason", "设备已移除或身份已变化，请重新选择"}});
                probing_devices_ = false;
                emit decoderDevicesChanged();
            },
            Qt::QueuedConnection);
    }));
    probe_thread_->start();
}

bool VideoReceiver::configure(const QString& address, int port, int index_base) {
    return configure(address, port, index_base, {});
}

bool VideoReceiver::configure(const QString& address, int port, int index_base,
                              const QString& decoder_choice) {
    const auto choice = decoder_choice.isEmpty() ? decoder_choice_ : decoder_choice;
    if (!validConfiguration(address.trimmed(), port, index_base) || !validDecoderChoice(choice)) {
        configuration_error_ = "请输入数字 IP、1–65535 端口、0/1 分片起始序号及有效解码设备选择";
        error_ = configuration_error_;
        emit statusChanged();
        return false;
    }
    // 整组原子替换而非合并损坏的旧 INI；保存失败不触碰正在运行的 socket。
    QSaveFile settings(config_path_);
    const auto contents = QString("[General]\naddress=%1\nport=%2\nindex_base=%3\ndecoder=%4\n")
                              .arg(address.trimmed())
                              .arg(port)
                              .arg(index_base)
                              .arg(choice)
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
    decoder_choice_ = choice;
    config_valid_ = true;
    configuration_error_.clear();
    error_.clear();
    emit configurationChanged();
    emit decoderDevicesChanged();
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
        last_decoded_us_ = -1;
    }
    running_ = true;
    configuration_error_.clear();
    error_.clear();
    status_ = "正在监听";
    worker_.reset(QThread::create(
        [this, address = address_, port = port_, base = index_base_, choice = decoder_choice_,
         generation = generation_] { receive(address, port, base, choice, generation); }));
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

void VideoReceiver::receive(const QString& address, int port, int index_base, const QString& choice,
                            quint64 generation) {
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
    VideoDecodeQueue decode_queue;
    const auto origin_us = monotonicUs();
    // 解码器及硬件对象只属于此子线程。声明在 queue 之后，退出先 stop/join 再销毁 queue。
    // 接收线程不等待驱动调用；停止仍需等待当前解码调用完成，无法强制取消 OS/驱动内部等待。
    std::jthread decode_worker([&](std::stop_token stop) {
        // 环境变量保留部署/测试覆盖；诊断明确报告覆盖，GUI 已保存选择不会被改写。
        const auto override_backend = qEnvironmentVariable("PIP_VIDEO_DECODER");
        HevcDecoder decoder(
            override_backend.isEmpty() ? (choice == "auto" || choice == "cpu" ? choice : "selected")
                                       : override_backend,
            override_backend.isEmpty() ? choice : qEnvironmentVariable("PIP_VIDEO_DEVICE"));
        if (!decoder.error().isEmpty()) {
            QMutexLocker lock(&mutex_);
            pending_error_ = decoder.error();
            failed_ = true;
            requestPollLocked(generation);
            return;
        }
        VideoStatistics decoded_statistics(origin_us);
        VideoTimingStatistics timing_statistics;
        quint64 epoch = decode_queue.epoch();
        qint64 next_diagnostics_us = 0;
        double queue_wait_ms = -1;
        while (!stop.stop_requested()) {
            const auto unit = decode_queue.take();
            // stop 会唤醒 take；退出时不要因代数变化再次重开 codec/调用驱动。
            if (stop.stop_requested()) break;
            const auto current_epoch = decode_queue.epoch();
            if (epoch != current_epoch) {
                // 生产者发布新代必须先持邮箱锁；在同锁内捕获原因，避免与后继重置错配。
                QVariantMap reset_context;
                {
                    QMutexLocker lock(&mutex_);
                    if (stop.stop_requested()) break;
                    reset_context = decode_queue.resetContext();
                    reset_context["previousEpoch"] = QVariant::fromValue(epoch);
                }
                decoded_statistics.reset(monotonicUs());
                timing_statistics.reset();
                queue_wait_ms = -1;
                epoch = reset_context.value("epoch").toULongLong();
                // 即使暂时没有新 AU，也及时清除旧来源的参考链和统计。
                // 重开设备上下文不持邮箱锁，收包无需等待 FFmpeg/驱动。
                const auto reset_began_us = monotonicUs();
                decoder.reset();
                const auto reset_ended_us = monotonicUs();
                {
                    QMutexLocker lock(&mutex_);
                    reset_context["startedUs"] = reset_began_us;
                    // 驱动调用结束即停表，不把后续等待邮箱锁误算成解码器重置成本。
                    reset_context["durationMs"] = (reset_ended_us - reset_began_us) / 1000.0;
                    reset_context["error"] = decoder.error().left(256);
                    recordRecoveryLocked("decoder_reset", reset_context, generation);
                }
                if (!decoder.error().isEmpty()) {
                    QMutexLocker lock(&mutex_);
                    pending_error_ = decoder.error();
                    failed_ = true;
                    requestPollLocked(generation);
                    return;
                }
            }
            if (unit && unit->epoch == epoch) {
                const auto began_us = monotonicUs();
                auto decoded = decoder.decode(unit->bytes, unit->received_us, unit->first_packet_us,
                                              unit->frame_id);
                const auto ended_us = monotonicUs();
                QMutexLocker lock(&mutex_);
                // 来源切换/缺帧/队列溢出可在解码期间发生。旧代输出不能覆盖清屏或新来源。
                if (unit->epoch == decode_queue.epoch() && !stop.stop_requested()) {
                    decoded_statistics.decodeFinished(ended_us - began_us, !decoded.image.isNull(),
                                                      ended_us);
                    queue_wait_ms = double(began_us - unit->received_us) / 1000;
                    const auto costs = decoder.timings();
                    timing_statistics.record(VideoStage::DecodeTotal, costs.total_us, ended_us);
                    if (unit->first_packet_us >= 0)
                        timing_statistics.record(VideoStage::Assembly,
                                                 unit->received_us - unit->first_packet_us,
                                                 ended_us);
                    timing_statistics.record(VideoStage::Codec, costs.codec_us, ended_us);
                    timing_statistics.record(VideoStage::Download, costs.download_us, ended_us);
                    timing_statistics.record(VideoStage::Convert, costs.convert_us, ended_us);
                    timing_statistics.record(VideoStage::Queue, began_us - unit->received_us,
                                             ended_us);
                    if (!decoder.error().isEmpty()) pending_error_ = decoder.error();
                    if (!decoded.image.isNull()) {
                        decoded.decoded_us = ended_us;
                        pending_frame_ = std::move(decoded);
                        ++revision_;
                        last_decoded_us_ = ended_us;
                        pending_status_ = "正在接收";
                        pending_error_.clear();
                    }
                    requestPollLocked(generation);
                }
            }
            const auto now_us = monotonicUs();
            if (now_us >= next_diagnostics_us) {
                const auto snapshot = decoded_statistics.snapshot(now_us);
                QMutexLocker lock(&mutex_);
                // 代数检查与发布同属邮箱临界区；网络失效操作也先持此锁，再取 queue 锁。
                const bool is_current = epoch == decode_queue.epoch();
                for (const auto* key : {"decodeFps", "decodeMs", "decodeMaxMs"})
                    pending_diagnostics_[key] = is_current ? snapshot.value(key) : QVariant(-1.0);
                // 只随统计快照发布，避免逐帧改变 QVariantMap 导致 QML 诊断文字高频刷新。
                pending_diagnostics_["decodeQueueMs"] = is_current ? queue_wait_ms : -1;
                pending_diagnostics_["timings"] =
                    is_current ? timing_statistics.snapshot(now_us) : QVariantMap{};
                pending_diagnostics_["decoderChoice"] = choice;
                pending_diagnostics_["decoderOverride"] = override_backend;
                pending_diagnostics_["decoder"] = decoder.backend();
                pending_diagnostics_["decoderDevice"] = decoder.device();
                pending_diagnostics_["hardwareActive"] = decoder.hardwareActive();
                pending_diagnostics_["decoderFallback"] = decoder.fallbackReason();
                pending_diagnostics_["decodeQueueDropped"] =
                    QVariant::fromValue(decode_queue.dropped());
                requestPollLocked(generation);
                next_diagnostics_us = now_us + 250000;
            }
        }
    });
    VideoAssembler assembler(index_base);
    QElapsedTimer clock;
    clock.start();
    VideoStatistics statistics(origin_us);
    qint64 next_statistics_us = 0;
    QHostAddress source;
    quint16 source_port = 0;
    qint64 last_packet_ms = -1;
    bool has_unit = false;
    quint16 last_unit = 0;
    while (!QThread::currentThread()->isInterruptionRequested()) {
        if (!socket.hasPendingDatagrams()) {
            socket.waitForReadyRead(10);
        }
        const auto now = clock.elapsed();
        assembler.expire(now);
        const auto timestamp_us = monotonicUs();
        if (timestamp_us >= next_statistics_us) {
            const auto counters = assembler.counters();
            statistics.assemblyCounters(counters.invalid, counters.duplicates, counters.incomplete,
                                        timestamp_us);
            auto snapshot = statistics.snapshot(timestamp_us);
            QMutexLocker lock(&mutex_);
            // 两线程只更新各自字段，不能以网络快照覆盖解码统计。
            for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
                if (!it.key().startsWith("decode")) pending_diagnostics_[it.key()] = it.value();
            requestPollLocked(generation);
            next_statistics_us = timestamp_us + 250000;
        }
        // 来源断流 1 秒后解除锁定，并丢掉解码参考帧；不混入其他发送端的分片。
        if (last_packet_ms >= 0 && now - last_packet_ms > 1000) {
            const int previous_unit = has_unit ? last_unit : -1;
            source.clear();
            assembler = VideoAssembler(index_base);
            statistics.reset(monotonicUs());
            next_statistics_us = 0;
            last_packet_ms = -1;
            has_unit = false;
            // 来源状态和图像邮箱分离；即使没有成功解码，也不能一直展示过期来源。
            QMutexLocker lock(&mutex_);
            decode_queue.reset("source_timeout", previous_unit);
            auto reset_context = decode_queue.resetContext();
            reset_context["status"] = "图传中断，等待画面";
            recordRecoveryLocked("queue_reset", reset_context, generation);
            pending_frame_ = {};
            ++revision_;
            last_decoded_us_ = -1;
            pending_status_ = "图传中断，等待画面";
            pending_diagnostics_["decodeQueueMs"] = -1.0;
            pending_source_address_.clear();
            pending_source_port_ = 0;
            requestPollLocked(generation);
        }
        {
            QMutexLocker lock(&mutex_);
            if (last_decoded_us_ >= 0 && monotonicUs() - last_decoded_us_ > 1000000) {
                auto context = decode_queue.resetContext();
                context["reason"] = "decoded_output_timeout";
                context["frameId"] = has_unit ? last_unit : -1;
                context["idleMs"] = (monotonicUs() - last_decoded_us_) / 1000.0;
                recordRecoveryLocked("output_timeout", context, generation);
                pending_frame_ = {};
                ++revision_;
                pending_status_ = "图传中断，等待画面";
                last_decoded_us_ = -1;
                requestPollLocked(generation);
            }
        }
        // 每轮有界处理，洪泛时也能及时观察停止与超时，不在 GUI 线程排逐包信号。
        for (int count = 0; count < 128 && socket.hasPendingDatagrams() &&
                            !QThread::currentThread()->isInterruptionRequested();
             ++count) {
            const auto packet = socket.receiveDatagram(1401);
            const auto packet_us = monotonicUs();
            // 统计仅含当前锁定来源；未知来源首个格式有效包在下面锁定时纳入。
            const bool from_source = !source.isNull() && packet.senderAddress() == source &&
                                     packet.senderPort() == source_port;
            if (from_source) statistics.datagram(packet.data().size(), monotonicUs());
            if (packet.data().size() <= 8 || packet.data().size() > 1400) {
                if (from_source) assembler.push(packet.data(), clock.elapsed());
                continue;
            }
            if (!source.isNull() &&
                (packet.senderAddress() != source || packet.senderPort() != source_port)) {
                continue;
            }
            // 第一个格式上可能有效的数据报锁定来源；停止/超时后允许发送端重启。
            const auto unit = assembler.push(packet.data(), clock.elapsed(), packet_us);
            const auto assembled_us = monotonicUs();
            if (!unit.isEmpty() || assembler.pendingCount() > 0) {
                if (source.isNull()) {
                    statistics.datagram(packet.data().size(), monotonicUs());
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
            statistics.completeFrame(unit_id, monotonicUs());
            if (has_unit && static_cast<quint16>(unit_id - last_unit) != 1) {
                // 丢弃旧参考帧，保留设备和失败历史；网络抖动不能触发昂贵的 GPU 重新探测。
                QMutexLocker lock(&mutex_);
                decode_queue.reset("frame_gap", unit_id, last_unit);
                auto reset_context = decode_queue.resetContext();
                reset_context["status"] = "等待关键帧";
                recordRecoveryLocked("queue_reset", reset_context, generation);
                pending_frame_ = {};
                ++revision_;
                last_decoded_us_ = -1;
                pending_diagnostics_["decodeQueueMs"] = -1.0;
                pending_status_ = "等待关键帧";
                requestPollLocked(generation);
            }
            has_unit = true;
            last_unit = unit_id;
            {
                QMutexLocker lock(&mutex_);
                const auto previous_epoch = decode_queue.epoch();
                decode_queue.push(unit, assembled_us, assembler.firstPacketUs(), unit_id);
                if (previous_epoch != decode_queue.epoch()) {
                    auto reset_context = decode_queue.resetContext();
                    reset_context["status"] = "等待关键帧";
                    recordRecoveryLocked("queue_reset", reset_context, generation);
                    // 过载丢掉待处理参考链，不能让在途旧帧在清屏后重新出现。
                    pending_frame_ = {};
                    ++revision_;
                    last_decoded_us_ = -1;
                    pending_status_ = "等待关键帧";
                    pending_diagnostics_["decodeQueueMs"] = -1.0;
                    requestPollLocked(generation);
                }
            }
        }
    }
    decode_worker.request_stop();
    {
        QMutexLocker lock(&mutex_);
        decode_queue.stop();
        recordRecoveryLocked("queue_reset", decode_queue.resetContext(), generation);
    }
    decode_worker.join();
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
            if (!frame_.image.isNull()) frame_.gui_received_us = monotonicUs();
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
