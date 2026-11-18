#include "serial_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QStandardPaths>
#include <chrono>
#include <utility>

#include "data_pipeline.h"
#include "serial_frame_parser.h"

namespace {
qint64 nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QVariantMap defaultConfiguration() {
    return {{"portName", ""},   {"baudRate", 921600}, {"dataBits", 8},
            {"parity", "none"}, {"stopBits", "1"},    {"flowControl", "none"}};
}

QString validate(const QVariantMap& config) {
    const auto name = config.value("portName").toString();
    if (name.isEmpty() || name.size() > 1024 || name != name.trimmed() ||
        name.contains(QChar::Null))
        return "请选择或填写串口设备";
    // 波特率采用工程范围 1..4000000；实际硬件/系统不支持时由 open/set 返回明确错误。
    const auto baud = config.value("baudRate").toInt();
    const auto bits = config.value("dataBits").toInt();
    if (baud < 1 || baud > 4000000 || config.value("baudRate").toDouble() != baud || bits < 5 ||
        bits > 8 || config.value("dataBits").toDouble() != bits)
        return "波特率须为 1–4000000 的整数，数据位须为 5–8";
    if (!QStringList{"none", "even", "odd", "space", "mark"}.contains(
            config.value("parity").toString()) ||
        !QStringList{"1", "1.5", "2"}.contains(config.value("stopBits").toString()) ||
        !QStringList{"none", "hardware", "software"}.contains(
            config.value("flowControl").toString()))
        return "校验位、停止位或流控无效";
    // Windows 1.5 停止位要求 5 数据位；在两平台使用同一配置校验，驱动仍可拒绝此组合。
    if ((config.value("stopBits") == "1.5" && bits != 5) ||
        (config.value("stopBits") == "2" && bits == 5))
        return "1.5 停止位须搭配 5 数据位；5 数据位不能搭配 2 停止位";
    return {};
}
}  // namespace

/** 串口到GUI的统计邮箱；mutex保护所有字段，不保存控制负载或逐帧向GUI排队。 */
struct SerialMailbox {
    QMutex mutex;
    qint64 last_frame_us = -1;  ///< 最近有效帧的本机单调时刻，不代表控制器生成时间。
    bool is_open = false;
    QString error;
    quint64 bytes = 0;
    quint64 accepted = 0;
    quint64 errors = 0;
    quint64 sequence_drops = 0;
    quint64 offline_drops = 0;       ///< 未建立当前MQTT通道时直接丢弃的有效输入，不缓存。
    quint64 reset_generation = 0;    ///< GUI 每次使旧输入失效时递增；mutex 保护。
    quint64 cleared_generation = 0;  ///< 工作线程完成驱动/残帧清理的代次；一致前禁止交付。
};

/** 私有串口工作对象，QSerialPort/定时器/解析器只在其所属线程访问和销毁。 */
class SerialWorker : public QObject {
   public:
    SerialWorker(QVariantMap configuration, std::shared_ptr<SerialMailbox> mailbox,
                 std::shared_ptr<CustomControlChannel> channel)
        : configuration_(std::move(configuration)),
          mailbox_(std::move(mailbox)),
          channel_(std::move(channel)) {}

    void begin() {
        port_ = new QSerialPort(this);
        timer_ = new QTimer(this);
        timer_->setInterval(25);
        connect(timer_, &QTimer::timeout, this, [this] { receive(); });
        connect(port_, &QSerialPort::readyRead, this, [this] { receive(); });
        connect(port_, &QSerialPort::errorOccurred, this,
                [this](QSerialPort::SerialPortError error) {
                    if (error != QSerialPort::NoError) fail(port_->errorString());
                });
        port_->setPortName(configuration_.value("portName").toString());
        port_->setReadBufferSize(4096);
        const auto parity = configuration_.value("parity").toString();
        const auto stop = configuration_.value("stopBits").toString();
        const auto flow = configuration_.value("flowControl").toString();
        // 不使用平台原生句柄；每个参数都检查结果，不能偷偷回退至驱动默认值。
        const bool configured =
            port_->setBaudRate(configuration_.value("baudRate").toInt()) &&
            port_->setDataBits(QSerialPort::DataBits(configuration_.value("dataBits").toInt())) &&
            port_->setParity(parity == "even"    ? QSerialPort::EvenParity
                             : parity == "odd"   ? QSerialPort::OddParity
                             : parity == "space" ? QSerialPort::SpaceParity
                             : parity == "mark"  ? QSerialPort::MarkParity
                                                 : QSerialPort::NoParity) &&
            port_->setStopBits(stop == "2"     ? QSerialPort::TwoStop
                               : stop == "1.5" ? QSerialPort::OneAndHalfStop
                                               : QSerialPort::OneStop) &&
            port_->setFlowControl(flow == "hardware"   ? QSerialPort::HardwareControl
                                  : flow == "software" ? QSerialPort::SoftwareControl
                                                       : QSerialPort::NoFlowControl);
        if (!configured || !port_->open(QIODevice::ReadOnly)) {
            fail(port_->errorString());
            return;
        }
        // 清掉驱动内打开前积压的输入，连接后只接受本代新数据。
        if (!port_->clear(QSerialPort::Input)) {
            fail(port_->errorString());
            return;
        }
        {
            QMutexLocker lock(&mailbox_->mutex);
            mailbox_->is_open = true;
            mailbox_->error.clear();
        }
        timer_->start();
    }

    void stop() {
        if (timer_) timer_->stop();
        if (port_) {
            disconnect(port_, nullptr, this, nullptr);
            // 打开失败时保留驱动原始原因；关闭未打开的设备会把它覆盖成 NotOpenError。
            if (port_->isOpen()) port_->close();
        }
        QMutexLocker lock(&mailbox_->mutex);
        mailbox_->is_open = false;
        channel_.reset();
    }

    void discardInput(quint64 generation, std::shared_ptr<CustomControlChannel> channel) {
        // MQTT 身份/连接变化时连残帧也失效，防止离线帧的尾部在新连接中被当作新指令。
        parser_.resetStream();
        if (port_ && port_->isOpen() && !port_->clear(QSerialPort::Input))
            fail(port_->errorString());
        QMutexLocker lock(&mailbox_->mutex);
        mailbox_->cleared_generation = generation;
        channel_ = std::move(channel);
    }

   private:
    void fail(const QString& reason) {
        // errorOccurred 可从 open/read 内同步发射；先停止入口，再丢弃本代控制数据。
        stop();
        QMutexLocker lock(&mailbox_->mutex);
        mailbox_->error = configuration_.value("portName").toString() + "：" + reason;
    }

    void receive() {
        if (!port_->isOpen()) return;
        const auto bytes = port_->read(4096);
        if (!port_->isOpen()) return;
        const auto now = nowUs();
        const auto frames = parser_.feed(bytes, now / 1000);
        bool can_forward;
        {
            QMutexLocker lock(&mailbox_->mutex);
            mailbox_->bytes += bytes.size();
            mailbox_->accepted = parser_.acceptedCount();
            mailbox_->errors = parser_.errorCount();
            mailbox_->sequence_drops = parser_.discardedSequenceCount();
            can_forward = channel_ && mailbox_->cleared_generation == mailbox_->reset_generation;
            if (!frames.isEmpty()) mailbox_->last_frame_us = now;
            if (!can_forward) mailbox_->offline_drops += frames.size();
        }
        // 有效帧直接交给原MQTT线程，GUI阻塞不再覆盖输入或等待结果；不新建网络连接。
        // 共享队列/限频最新槽均有界，TTL从完整帧被本机解析的now起算，不推断设备生成时间。
        if (can_forward)
            for (const auto& frame : frames) channel_->submit(frame, now);
    }

    QVariantMap configuration_;
    std::shared_ptr<SerialMailbox> mailbox_;
    std::shared_ptr<CustomControlChannel> channel_;  ///< 仅本串口线程使用/替换其副本。
    QSerialPort* port_ = nullptr;
    QTimer* timer_ = nullptr;
    SerialFrameParser parser_;
};

SerialController::SerialController(DataPipeline& pipeline, const QString& config_path,
                                   QObject* parent)
    : QObject(parent),
      pipeline_(pipeline),
      config_path_(config_path.isEmpty()
                       ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                             .filePath("serial.json")
                       : config_path),
      configuration_(defaultConfiguration()) {
    is_config_valid_ = load();
    refreshPortsAsync();
    poll_timer_.setInterval(14);
    poll_timer_.setTimerType(Qt::PreciseTimer);
    connect(&poll_timer_, &QTimer::timeout, this, &SerialController::poll);
    connect(&pipeline_, &DataPipeline::snapshotsReset, this, [this] {
        // 同一身份重连和新身份都必须丢弃未发控制，不能仅检查 GUI 当前 ready 字符串。
        discardPending();
    });
    connect(&pipeline_, &DataPipeline::connectionStateChanged, this, [this] {
        discardPending();
        if (worker_) poll();
    });
}

SerialController::~SerialController() {
    if (ports_probe_) ports_probe_->wait();
    stop();
}
QVariantMap SerialController::configuration() const { return configuration_; }
QVariantList SerialController::ports() const { return ports_; }
QString SerialController::state() const { return state_; }
QString SerialController::forwardingState() const { return forwarding_state_; }
QString SerialController::error() const { return error_; }
QVariantMap SerialController::diagnostics() const { return diagnostics_; }

bool SerialController::load() {
    QFile file(config_path_);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        setError("无法读取串口配置，原文件不覆盖");
        return false;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    const auto root = document.object();
    const auto candidate = root.value("connection").toObject().toVariantMap();
    if (parse_error.error != QJsonParseError::NoError || !document.isObject() ||
        root.value("version").toInt(-1) != 1 || !validate(candidate).isEmpty()) {
        setError("串口配置损坏或版本不支持，原文件不覆盖");
        return false;
    }
    configuration_ = candidate;
    return true;
}

bool SerialController::configure(const QVariantMap& configuration) {
    const auto validation = validate(configuration);
    if (!validation.isEmpty()) {
        setError(validation);
        return false;
    }
    if (!is_config_valid_) return false;
    QFile existing(config_path_);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > 65536) {
            setError("无法读取原串口配置；未覆盖");
            return false;
        }
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(existing.readAll(), &parse_error);
        const auto root = document.object();
        if (parse_error.error != QJsonParseError::NoError || !document.isObject() ||
            root.value("version").toInt(-1) != 1 ||
            !validate(root.value("connection").toObject().toVariantMap()).isEmpty()) {
            setError("原串口配置损坏或版本不支持；未覆盖");
            return false;
        }
        // Windows 下替换前释放原文件句柄，否则 QSaveFile 的原子提交可能被拒绝。
        existing.close();
    }
    // 只保存明确支持的字段，协议命令字不能由配置或 QML 改写。
    auto candidate = defaultConfiguration();
    for (auto it = candidate.begin(); it != candidate.end(); ++it)
        it.value() = configuration.value(it.key());
    const auto bytes =
        QJsonDocument(
            QJsonObject{{"version", 1}, {"connection", QJsonObject::fromVariantMap(candidate)}})
            .toJson();
    QSaveFile file(config_path_);
    if (!QDir().mkpath(QFileInfo(config_path_).absolutePath()) ||
        !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError("无法原子保存串口配置，原连接保持不变");
        return false;
    }
    stop();
    configuration_ = candidate;
    setError({});
    emit configurationChanged();
    return true;
}

void SerialController::refreshPortsAsync() {
    if (ports_probe_ && ports_probe_->isRunning()) return;
    if (ports_probe_) ports_probe_->wait();
    // 设备驱动枚举可能等待 OS；只在 GUI 应用值快照，析构 join 保证 this 仍存活。
    ports_probe_.reset(QThread::create([this] {
        QVariantList ports;
        for (const auto& port : QSerialPortInfo::availablePorts()) {
            ports.append(QVariantMap{{"portName", port.systemLocation()},
                                     {"label", port.portName() + "  " + port.description()},
                                     {"serialNumber", port.serialNumber()},
                                     {"manufacturer", port.manufacturer()}});
        }
        QMetaObject::invokeMethod(
            this,
            [this, ports = std::move(ports)] {
                ports_ = ports;
                emit portsChanged();
            },
            Qt::QueuedConnection);
    }));
    ports_probe_->start();
}

bool SerialController::start() {
    const auto validation = validate(configuration_);
    if (!is_arena_ || !is_config_valid_ || !validation.isEmpty()) {
        setError(!is_arena_          ? "请在比赛页面连接控制器"
                 : !is_config_valid_ ? "请先修复串口配置文件"
                                     : validation);
        return false;
    }
    stop();
    error_.clear();
    mailbox_ = std::make_shared<SerialMailbox>();
    channel_ = is_forwarding_blocked_ ? nullptr : pipeline_.createCustomControlChannel();
    auto* worker = new SerialWorker(configuration_, mailbox_, channel_);
    worker_ = worker;
    worker->moveToThread(&thread_);
    connect(&thread_, &QThread::started, worker, &SerialWorker::begin);
    connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
    state_ = "connecting";
    forwarding_state_ = "等待串口";
    forwarding_totals_.clear();
    diagnostics_.clear();
    thread_.start();
    poll_timer_.start();
    emit statusChanged();
    emit diagnosticsChanged();
    return true;
}

void SerialController::discardPending() {
    // 先使来源通道失效并撤回待发项，再清理驱动/残帧并启用新代通道。
    // MQTT断线在其工作线程立即失效通道，不必等待GUI收到状态通知。
    if (channel_) {
        channel_->cancel();
        const auto counts = channel_->diagnostics();
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            if (it.key() == "lastResult")
                forwarding_totals_[it.key()] = it.value();
            else
                forwarding_totals_[it.key()] =
                    forwarding_totals_.value(it.key()).toULongLong() + it.value().toULongLong();
        }
        channel_.reset();
    }
    quint64 generation = 0;
    if (mailbox_) {
        QMutexLocker lock(&mailbox_->mutex);
        generation = ++mailbox_->reset_generation;
    }
    if (worker_) {
        channel_ = is_forwarding_blocked_ ? nullptr : pipeline_.createCustomControlChannel();
        auto* worker = static_cast<SerialWorker*>(worker_);
        QMetaObject::invokeMethod(
            worker,
            [worker, generation, channel = channel_] { worker->discardInput(generation, channel); },
            Qt::QueuedConnection);
    }
}

void SerialController::stop() {
    poll_timer_.stop();
    if (worker_) {
        auto* worker = static_cast<SerialWorker*>(worker_);
        QMetaObject::invokeMethod(
            worker, [worker] { worker->stop(); }, Qt::BlockingQueuedConnection);
        thread_.quit();
        thread_.wait();
        worker_ = nullptr;
    }
    discardPending();
    mailbox_.reset();
    state_ = "disconnected";
    forwarding_state_ = "未启用";
    emit statusChanged();
}

void SerialController::setArena(bool is_arena) {
    is_arena_ = is_arena;
    if (!is_arena) stop();
}

void SerialController::setForwardingBlocked(bool is_blocked) {
    if (is_forwarding_blocked_ == is_blocked) return;
    is_forwarding_blocked_ = is_blocked;
    // cancel 同步使旧来源失效；工作线程收到新代入口前不能提交积压输入。
    discardPending();
    if (worker_) poll();
}

void SerialController::setError(const QString& error) {
    if (error_ == error) return;
    error_ = error;
    emit statusChanged();
}

void SerialController::poll() {
    if (!mailbox_) return;
    bool is_open;
    QString serial_error;
    qint64 last_frame;
    QVariantMap snapshot;
    {
        QMutexLocker lock(&mailbox_->mutex);
        is_open = mailbox_->is_open;
        serial_error = mailbox_->error;
        last_frame = mailbox_->last_frame_us;
        snapshot = {
            {"receivedBytes", mailbox_->bytes},        {"acceptedFrames", mailbox_->accepted},
            {"frameErrors", mailbox_->errors},         {"sequenceDrops", mailbox_->sequence_drops},
            {"offlineDrops", mailbox_->offline_drops}, {"workerCoalescedFrames", quint64(0)},
            {"guiCoalescedFrames", quint64(0)}};
    }
    const auto current_counts = channel_ ? channel_->diagnostics() : QVariantMap{};
    for (const auto* name : {"submittedCount", "sentCount", "failedCount", "offlineDrops",
                             "staleDrops", "coalescedFrames", "cancelledCount"}) {
        snapshot[name] = snapshot.value(name).toULongLong() +
                         forwarding_totals_.value(name).toULongLong() +
                         current_counts.value(name).toULongLong();
    }
    snapshot["mqttCoalescedFrames"] = snapshot.value("coalescedFrames");
    const auto last_result =
        current_counts.value("lastResult", forwarding_totals_.value("lastResult")).toString();
    snapshot["lastResult"] = last_result;
    // GUI只展示状态/诊断；不保存控制数据，也不决定发送或完成时刻。
    const auto now = nowUs();
    const auto new_state = !serial_error.isEmpty()                        ? QString("error")
                           : !is_open                                     ? QString("connecting")
                           : last_frame < 0 || now - last_frame >= 100000 ? QString("waiting")
                                                                          : QString("receiving");
    const bool is_ready = pipeline_.connectionState() == "ready";
    const auto forwarding =
        !is_open                   ? QString("等待串口")
        : is_forwarding_blocked_   ? QString("自检中，不转发")
        : !is_ready                ? QString("等待 MQTT")
        : new_state != "receiving" ? QString("等待新数据")
        : !last_result.isEmpty() && last_result != "sent" && last_result != "superseded"
            ? QString("发送失败")
            : QString("转发中");
    if (state_ != new_state || forwarding_state_ != forwarding ||
        (!serial_error.isEmpty() && error_ != serial_error)) {
        state_ = new_state;
        forwarding_state_ = forwarding;
        // 接收轮询不清除尚未修正的表单/保存错误，只有成功配置或重连才清除。
        if (!serial_error.isEmpty()) error_ = serial_error;
        emit statusChanged();
    }
    if (now - last_diagnostics_us_ >= 250000) {
        last_diagnostics_us_ = now;
        snapshot.insert("state", state_);
        snapshot.insert("forwardingState", forwarding_state_);
        diagnostics_ = snapshot;
        emit diagnosticsChanged();
    }
}
