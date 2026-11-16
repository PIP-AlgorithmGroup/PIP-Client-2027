#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>

#include "communication/data_pipeline.h"
#include "communication/serial_controller.h"
#include "input/input_controller.h"
#include "video/video_receiver.h"

/** 唯一一套本地连接配置，不作为官方 payload；地址初始值采用官方约定，均可修改。 */
struct SessionSettings {
    QString interface_name;  ///< QNetworkInterface::name，不保存易变列表索引。
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
    /// 后台保存并连接尚未完成；重复提交被拒绝，stop 取消连接意图。
    Q_PROPERTY(bool connecting READ connecting NOTIFY statusChanged)
    /// 会话拥有的控制器入口；QML 只配置/观察，不持有设备或解析原始字节。
    Q_PROPERTY(SerialController* serialController READ serialController CONSTANT)

    /// 会话拥有的原生键鼠入口，QML 不拥有输入线程或系统句柄。
    Q_PROPERTY(InputController* inputController READ inputController CONSTANT)

   public:
    /** 网卡健康/异步连接校验探针：接收独立配置快照，返回接口/IP 是否就绪。
     * 仅后台线程调用，不得访问 GUI/本会话；捕获对象须存活到会话析构完成。
     */
    using NetworkReadyProbe = std::function<bool(const SessionSettings&)>;
    /** 构造读取本地配置并异步枚举接口；空路径使用 AppConfigLocation/session.json。
     * 空探针使用 Qt 系统接口查询；探针用于周期检测和异步连接，可能并发调用；同步 C++
     * 保存/启动仍真实校验。
     */
    MatchSession(DataPipeline& pipeline, VideoReceiver& video, const QString& config_path = {},
                 QObject* parent = nullptr, NetworkReadyProbe network_probe = {});
    /** 停止周期检测并 join 当前系统查询，再停 MQTT/视频；只在所属线程析构。
     * OS 网卡查询不能强制取消；不遗留捕获 this 的后台线程或回调。
     */
    ~MatchSession() override;
    /** 以下 getter 只返回 GUI 快照，不访问网络，仅所属线程调用。 */
    QVariantMap configuration() const;
    QVariantList interfaces() const;
    bool active() const;
    QString error() const;
    bool networkReady() const;
    /** GUI 读取异步操作状态，不等待查询或写盘。 */
    bool connecting() const;
    /** 返回会话拥有的 GUI 对象，不转移所有权；有效期不超过本会话。 */
    SerialController* serialController();
    /** 借用 GUI facade，生命周期不超过会话；仅所属线程访问。 */
    InputController* inputController();
    /** 返回 red/blue 阵营的合法兵种和实际机器人 ID；未知阵营返回空，仅所属线程调用，无 I/O。 */
    Q_INVOKABLE QVariantList robotOptions(const QString& team) const;
    /** 显式同步枚举系统网卡列表；不自动选择/修改接口。驱动查询可能阻塞调用线程。
     * 周期健康检测独立异步运行，不调用此用户刷新入口。
     */
    void refreshInterfaces();
    /** GUI 提交异步网卡枚举；忙时合并，结果只在所属线程应用。 */
    Q_INVOKABLE void refreshInterfacesAsync();
    /** 提交保存并连接，返回任务是否接受；最终由 configuration/statusChanged 报告。
     * 后台校验/原子写盘；离页或 stop 后仍兑现保存，但不会恢复连接意图。
     */
    Q_INVOKABLE bool connectAsync(const QString& interface_name, const QString& local_address,
                                  int robot_id, const QString& broker_host, int broker_port);
    /** 整组校验和原子保存；失败保持原会话，成功 active 时按新身份重建连接。 */
    bool configure(const QString& interface_name, const QString& local_address, int robot_id,
                   const QString& broker_host, int broker_port);
    /** 比赛页明确启动 MQTT，false 时 error 解释原因，true 不代表 Broker 已连接。 */
    bool start();
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
    static QString validateNetwork(const SessionSettings& settings);
    bool load(SessionSettings& settings);
    bool save(const SessionSettings& settings);
    static QString saveConfiguration(const QString& path, const SessionSettings& settings);
    bool launch();
    void checkNetwork();
    void pollNetwork();
    void applyNetworkReady(bool ready);
    void setError(const QString& error);

    DataPipeline& pipeline_;  ///< GUI 引用，外部保证生命周期。
    VideoReceiver& video_;    ///< GUI facade，不跨线程访问解码器。
    QString config_path_;
    SerialController serial_controller_;  ///< 在 pipeline 之前停止/析构，配置与 MQTT 独立保存。
    InputController input_controller_;  ///< 先于 pipeline 析构，原生 surface 销毁前 join。
    SessionSettings settings_;
    QVariantList interfaces_;
    QTimer network_timer_;  ///< 500ms 发起异步健康检测，GUI 不执行周期系统接口枚举。
    NetworkReadyProbe network_probe_;  ///< 构造后不修改；每次后台任务独占调用副本。
    std::unique_ptr<QThread> network_probe_thread_;  ///< 最多一个查询在途，繁忙时合并轮询。
    quint64 network_revision_ = 0;  ///< GUI 独有；配置/手动刷新变化后丢弃旧查询结果。
    std::unique_ptr<QThread> interfaces_thread_,
        connect_thread_;  ///< 单任务后台查询/写盘；析构 join。
    quint64 connect_revision_ = 0;  ///< stop/离页取消在途操作的连接意图，不取消已接受的保存。
    bool connecting_ = false;
    bool active_ = false;
    bool network_ready_ = false;
    bool is_arena_ = false;
    bool is_rmul_ = false;
    bool network_suspended_ = false;
    bool video_enabled_ = false;  ///< RMUL 进入时开启；用户暂停后网络恢复不擅自重启。
    QString error_;
};
