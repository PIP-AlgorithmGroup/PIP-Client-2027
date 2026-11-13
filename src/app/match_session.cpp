#include "app/match_session.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include "core/protocol_catalog.h"

namespace {
bool isRobotValid(int id) {
    const auto number = id > 100 ? id - 100 : id;
    return number >= 1 && number <= 9 && number != 2;
}

QJsonObject toJson(const SessionSettings& settings) {
    return {{"interfaceName", settings.interface_name},
            {"localAddress", settings.local_address},
            {"brokerHost", settings.broker_host},
            {"brokerPort", settings.broker_port},
            {"robotId", settings.robot_id}};
}
}  // namespace

MatchSession::MatchSession(DataPipeline& pipeline, VideoReceiver& video, const QString& config_path,
                           QObject* parent)
    : QObject(parent),
      pipeline_(pipeline),
      video_(video),
      config_path_(config_path.isEmpty()
                       ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                             .filePath("session.json")
                       : config_path) {
    SessionSettings initial;
    // 损坏文件不能把部分解析结果带入可运行配置。
    if (load(initial)) settings_ = initial;
    refreshInterfaces();
    network_timer_.setInterval(500);
    connect(&network_timer_, &QTimer::timeout, this, &MatchSession::checkNetwork);
    network_timer_.start();
}

MatchSession::~MatchSession() {
    network_timer_.stop();
    stop();
    video_.stop();
}

QVariantMap MatchSession::configuration() const {
    auto result = toJson(settings_).toVariantMap();
    result["clientId"] = QString::number(settings_.robot_id);
    return result;
}
QVariantList MatchSession::interfaces() const { return interfaces_; }
bool MatchSession::active() const { return active_; }
QString MatchSession::error() const { return error_; }
bool MatchSession::networkReady() const { return network_ready_; }

QVariantList MatchSession::robotOptions(const QString& team) const {
    if (team != "red" && team != "blue") return {};
    // 官方附录 ID：2/102 保留；步兵三个编号独立列出，不能只按兵种推定唯一身份。
    const int offset = team == "blue" ? 100 : 0;
    QVariantList result;
    const QStringList labels = {"重装",     "步兵 3 号", "步兵 4 号", "步兵 5 号",
                                "空中系统", "哨兵",      "飞镖",      "雷达"};
    const int numbers[] = {1, 3, 4, 5, 6, 7, 8, 9};
    for (int index = 0; index < labels.size(); ++index) {
        result.append(QVariantMap{{"label", labels[index]}, {"robotId", numbers[index] + offset}});
    }
    return result;
}

void MatchSession::setError(const QString& error) {
    if (error_ != error) {
        error_ = error;
        emit statusChanged();
    }
}

QString MatchSession::validate(const SessionSettings& settings, bool check_network) const {
    const QHostAddress address(settings.local_address);
    if (!isRobotValid(settings.robot_id) || settings.interface_name.isEmpty() ||
        address.protocol() != QAbstractSocket::IPv4Protocol || address == QHostAddress::AnyIPv4 ||
        address.isMulticast() || address == QHostAddress::Broadcast ||
        settings.broker_host.isEmpty() || settings.broker_host != settings.broker_host.trimmed() ||
        settings.broker_host.contains(QChar::Null) || settings.broker_port < 1 ||
        settings.broker_port > 65535) {
        return "请选择网卡、有效 IPv4、机器人身份及 MQTT 地址/端口";
    }
    return check_network ? validateNetwork(settings) : QString{};
}

QString MatchSession::validateNetwork(const SessionSettings& settings) const {
    // 网卡就绪独立于机器人身份/Broker，不能用完整配置有效性冒充网络健康。
    const QHostAddress address(settings.local_address);
    const auto interface = QNetworkInterface::interfaceFromName(settings.interface_name);
    const auto flags = interface.flags();
    if (settings.interface_name.isEmpty() || address.protocol() != QAbstractSocket::IPv4Protocol ||
        !interface.isValid() || !flags.testFlag(QNetworkInterface::IsUp) ||
        !flags.testFlag(QNetworkInterface::IsRunning)) {
        return "所选网卡不可用，请检查接口连接状态";
    }
    bool found = false;
    for (const auto& entry : interface.addressEntries()) found |= entry.ip() == address;
    return found ? QString{}
                 : QString("所选网卡没有配置该 IPv4；请在系统网络设置中配置，不自动修改系统");
}

bool MatchSession::load(SessionSettings& settings) {
    settings = {};
    QFile file(config_path_);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        setError("无法读取接入配置，禁止自动连接");
        return false;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    const auto root = document.object();
    const auto version = root.value("version").toInt(-1);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject() ||
        (version != 1 && version != 2)) {
        setError("接入配置格式损坏，原配置不覆盖");
        return false;
    }
    // 旧版启动固定读取 official；有该项时沿用原选择，不能因其损坏静默切换到开发地址。
    // 仅有 development 时接续它；本次只读取，用户成功保存后才写成单配置 v2。
    const auto key = version == 2                ? QString("connection")
                     : root.contains("official") ? QString("official")
                                                 : QString("development");
    if (version == 1 && !root.contains(key)) return true;
    if (!root.value(key).isObject()) {
        setError("连接配置必须为对象");
        return false;
    }
    const auto item = root.value(key).toObject();
    settings.interface_name = item.value("interfaceName").toString();
    settings.local_address = item.value("localAddress").toString();
    settings.broker_host = item.value("brokerHost").toString();
    settings.broker_port = item.value("brokerPort").toInt(-1);
    settings.robot_id = item.value("robotId").toInt(-1);
    const auto error = validate(settings, false);
    if (!error.isEmpty()) {
        setError("接入配置损坏：" + error);
        return false;
    }
    return true;
}

bool MatchSession::save(const SessionSettings& settings) {
    QFile existing(config_path_);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > 65536) {
            setError("无法读取原接入配置；未保存");
            return false;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(existing.readAll(), &error);
        const auto version = document.object().value("version").toInt(-1);
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            (version != 1 && version != 2)) {
            setError("原接入配置损坏或版本不支持；未覆盖");
            return false;
        }
        // Windows 上仍打开的原文件会阻止 QSaveFile 原子替换；读取完成立即释放句柄。
        existing.close();
    }
    const QJsonObject root{{"version", 2}, {"connection", toJson(settings)}};
    const auto bytes = QJsonDocument(root).toJson();
    QSaveFile file(config_path_);
    if (!QDir().mkpath(QFileInfo(config_path_).absolutePath()) ||
        !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError("无法原子保存接入配置，原会话保持不变");
        return false;
    }
    return true;
}

void MatchSession::refreshInterfaces() {
    interfaces_.clear();
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        for (const auto& entry : interface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            interfaces_.append(QVariantMap{
                {"name", interface.name()},
                {"label", interface.humanReadableName() + " / " + entry.ip().toString()},
                {"address", entry.ip().toString()},
                {"prefixLength", entry.prefixLength()},
                {"isEthernet", interface.type() == QNetworkInterface::Ethernet},
                {"isUp", interface.flags().testFlag(QNetworkInterface::IsUp)}});
        }
    }
    emit interfacesChanged();
    checkNetwork();
}

bool MatchSession::configure(const QString& interface_name, const QString& local_address,
                             int robot_id, const QString& broker_host, int broker_port) {
    SessionSettings candidate{interface_name, local_address, broker_host, broker_port, robot_id};
    const auto error = validate(candidate, true);
    if (!error.isEmpty()) {
        setError(error);
        return false;
    }
    if (!save(candidate)) return false;
    const bool restart = active_;
    pipeline_.stop();
    settings_ = candidate;
    network_suspended_ = false;
    setError({});
    emit configurationChanged();
    checkNetwork();
    if (restart) return launch();
    return true;
}

bool MatchSession::launch() {
    MqttSettings mqtt;
    mqtt.host = settings_.broker_host;
    mqtt.port = quint16(settings_.broker_port);
    mqtt.client_id = QString::number(settings_.robot_id);
    mqtt.robot_id = quint32(settings_.robot_id);
    mqtt.local_address = QHostAddress(settings_.local_address);
    mqtt.is_official = true;
    mqtt.bindings = officialReceiveBindings();
    // 视频的保存/绑定失败只降级视频，不能拖垮独立 MQTT；也不能继续从旧网卡接收。
    if (is_rmul_ && video_enabled_) {
        if (video_.bindAddress() != settings_.local_address &&
            !video_.configure(settings_.local_address, video_.port(), video_.indexBase())) {
            video_.stop();
            setError("图传绑定配置失败：" + video_.error());
        } else {
            video_.start();
        }
    }
    if (!pipeline_.start(mqtt)) {
        setError(pipeline_.lastError());
        active_ = false;
        emit statusChanged();
        return false;
    }
    network_suspended_ = false;
    active_ = true;
    emit statusChanged();
    return true;
}

bool MatchSession::start() {
    if (!is_arena_) {
        setError("请先进入比赛页面");
        return false;
    }
    const auto error = validate(settings_, true);
    if (!error.isEmpty()) {
        setError(error);
        return false;
    }
    if (active_ && !network_suspended_) return true;
    setError({});
    return launch();
}

void MatchSession::stop() {
    active_ = false;
    network_suspended_ = false;
    pipeline_.stop();
    emit statusChanged();
}

void MatchSession::setArena(bool is_arena, const QString& mode) {
    const bool was_rmul = is_rmul_;
    is_arena_ = is_arena;
    is_rmul_ = is_arena && mode == "RMUL";
    if (!is_arena) {
        video_enabled_ = false;
        stop();
        video_.stop();
    } else if (is_rmul_ && !was_rmul) {
        video_enabled_ = true;
        startVideo();
    } else if (!is_rmul_) {
        video_enabled_ = false;
        video_.stop();
    }
}

bool MatchSession::configureVideo(const QString& address, int port, int index_base) {
    return configureVideo(address, port, index_base, {});
}

bool MatchSession::configureVideo(const QString& address, int port, int index_base,
                                  const QString& decoder_choice) {
    if (active_ && address.trimmed() != settings_.local_address) {
        setError("会话运行时视频必须使用同一个接入 IPv4，请在当前会话中修改网卡");
        return false;
    }
    if (!video_.configure(address, port, index_base, decoder_choice)) {
        setError(video_.error());
        return false;
    }
    setError({});
    return true;
}
void MatchSession::startVideo() {
    if (!is_rmul_) return;
    video_enabled_ = true;
    if (active_ && video_.bindAddress() != settings_.local_address &&
        !video_.configure(settings_.local_address, video_.port(), video_.indexBase())) {
        video_.stop();
        setError(video_.error());
        return;
    }
    video_.start();
}
void MatchSession::stopVideo() {
    video_enabled_ = false;
    video_.stop();
}

void MatchSession::checkNetwork() {
    const bool ready = validateNetwork(settings_).isEmpty();
    if (network_ready_ != ready) {
        network_ready_ = ready;
        emit statusChanged();
    }
    if (!active_) return;
    if (!ready && !network_suspended_) {
        // 网卡消失先清除 MQTT 数据与发送资格；视频线程自行报告 UDP 断流。
        network_suspended_ = true;
        pipeline_.stop();
        setError("所选接入网卡或 IPv4 已不可用，等待恢复");
    } else if (ready && network_suspended_) {
        setError({});
        launch();
    }
}
