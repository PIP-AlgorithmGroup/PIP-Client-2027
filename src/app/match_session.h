#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "communication/data_pipeline.h"
#include "communication/serial_controller.h"
#include "video/video_receiver.h"

/** 唯一一套本地连接配置，不作为官方 payload；地址初始值采用官方约定，均可修改。 */
struct SessionSettings {
    QString interface_name;                  ///< QNetworkInterface::name，不保存易变列表索引。
    QString local_address = "192.168.12.2";  ///< 选定接口实际 IPv4。
    QString broker_host = "192.168.12.1";    ///< MQTT 主机名或 IP，默认官方服务器。
    int broker_port = 3333;                  ///< TCP 端口 1..65535。
    int robot_id = 0;                        ///< 0 为未选，开始连接前须选择。
};

/**
 * GUI 线程会话协调器，注入对象必须比本对象活得久；析构停止但不删除注入对象。
 * 管理 MQTT 接入意图及视频/控制器页面生命周期；只转发用户连接的控制器新数据，不修改 OS 网络。
 */
class MatchSession : public QObject {
    Q_OBJECT
    /// 完整本地配置副本，不是官方 wire 消息。
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    /// 当前可枚举 IPv4 接口；回环仅供开发测试。
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY interfacesChanged)
    /// 用户启动意图，与实际网络连接状态分开。
    Q_PROPERTY(bool active READ active NOTIFY statusChanged)
    /// 最近配置或网卡错误，不含凭证。
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    /// 网卡 up/running 且具有预期 IP，不代表 Broker 或视频健康。
    Q_PROPERTY(bool networkReady READ networkReady NOTIFY statusChanged)
    /// 会话拥有的控制器入口；QML 只配置/观察，不持有设备或解析原始字节。
    Q_PROPERTY(SerialController* serialController READ serialController CONSTANT)

   public:
    /** 构造仅读取本地配置；空路径使用 AppConfigLocation/session.json。 */
    MatchSession(DataPipeline& pipeline, VideoReceiver& video, const QString& config_path = {},
                 QObject* parent = nullptr);
    /** 先停止本地检测，再停 MQTT/视频；只在所属线程析构。 */
    ~MatchSession() override;
    /** 以下 getter 只返回 GUI 快照，不访问网络，仅所属线程调用。 */
    QVariantMap configuration() const;
    QVariantList interfaces() const;
    bool active() const;
    QString error() const;
    bool networkReady() const;
    /** 返回会话拥有的 GUI 对象，不转移所有权；有效期不超过本会话。 */
    SerialController* serialController();
    /** 返回 red/blue 阵营的合法兵种和实际机器人 ID；未知阵营返回空，仅所属线程调用，无 I/O。 */
    Q_INVOKABLE QVariantList robotOptions(const QString& team) const;
    /** 刷新网卡列表；不自动选择或修改接口，不阻塞网络访问。 */
    Q_INVOKABLE void refreshInterfaces();
    /** 整组校验和原子保存；失败保持原会话，成功 active 时按新身份重建连接。 */
    Q_INVOKABLE bool configure(const QString& interface_name, const QString& local_address,
                               int robot_id, const QString& broker_host, int broker_port);
    /** 比赛页明确启动 MQTT，false 时 error 解释原因，true 不代表 Broker 已连接。 */
    Q_INVOKABLE bool start();
    /** 停止 MQTT 意图和业务数据；视频保持当前页行为。 */
    Q_INVOKABLE void stop();
    /** C++ 导航入口，RMUL 进入自动监听视频，离开停止全部链路。 */
    void setArena(bool is_arena, const QString& mode);
    /** 视频配置入口；active 时禁止绑定另一网卡，失败保持原配置。 */
    Q_INVOKABLE bool configureVideo(const QString& address, int port, int index_base);
    /** 与监听原子保存解码设备 key；复用会话地址约束，失败保持旧会话。 */
    Q_INVOKABLE bool configureVideo(const QString& address, int port, int index_base,
                                    const QString& decoder_choice);
    /** 比赛页面恢复视频，不隐式连接 MQTT。 */
    Q_INVOKABLE void startVideo();
    /** 仅暂停视频，不停止 MQTT。 */
    Q_INVOKABLE void stopVideo();

   signals:
    /** 配置已完整改变。 */
    void configurationChanged();
    /** 网卡列表已刷新。 */
    void interfacesChanged();
    /** 接入意图、网络或错误改变。 */
    void statusChanged();

   private:
    QString validate(const SessionSettings& settings, bool check_network) const;
    QString validateNetwork(const SessionSettings& settings) const;
    bool load(SessionSettings& settings);
    bool save(const SessionSettings& settings);
    bool launch();
    void checkNetwork();
    void setError(const QString& error);

    DataPipeline& pipeline_;  ///< GUI 引用，外部保证生命周期。
    VideoReceiver& video_;    ///< GUI facade，不跨线程访问解码器。
    QString config_path_;
    SerialController serial_controller_;  ///< 在 pipeline 之前停止/析构，配置与 MQTT 独立保存。
    SessionSettings settings_;
    QVariantList interfaces_;
    QTimer network_timer_;  ///< 500 ms 网络安全检测，不采集键鼠。
    bool active_ = false;
    bool network_ready_ = false;
    bool is_arena_ = false;
    bool is_rmul_ = false;
    bool network_suspended_ = false;
    bool video_enabled_ = false;  ///< RMUL 进入时开启；用户暂停后网络恢复不擅自重启。
    QString error_;
};
