#pragma once

#include <QByteArray>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DataPipeline;
struct SerialMailbox;

/**
 * 自定义控制器入口，公开 API 仅所属 GUI 线程调用；串口由专用工作线程独占。
 * 固定解析 0x0311，将最多 60 字节负载发布为 CustomControl，不解释其内部字段。
 * pipeline 必须比本对象长寿；析构关闭串口并 join，禁止在 QApplication 析构后使用。
 */
class SerialController : public QObject {
    Q_OBJECT
    /// 已保存参数副本；默认 921600/8N1/无流控，不含协议选项。
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    /// 系统枚举的实际设备路径和显示信息，不以列表索引保存身份。
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
    /// disconnected/connecting/waiting/receiving/error，仅表示本地串口。
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    /// 停用、等待 MQTT、等待新数据、转发中或发送失败；不表示机器人已执行。
    Q_PROPERTY(QString forwardingState READ forwardingState NOTIFY statusChanged)
    /// 配置/串口/驱动错误，不包含控制负载；手动重新连接时清除。
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    /// 有界接收、丢弃和 MQTT 本地发送统计，约 250 ms 更新一次。
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)

   public:
    /** 仅读取配置，不打开端口；空路径使用 AppConfigLocation/serial.json。 */
    explicit SerialController(DataPipeline& pipeline, const QString& config_path = {},
                              QObject* parent = nullptr);
    /** 同步关闭并等待串口线程退出，不等待远端设备响应。 */
    ~SerialController() override;
    /** 以下 getter 只返回所属 GUI 线程快照，不访问串口句柄。 */
    QVariantMap configuration() const;
    QVariantList ports() const;
    QString state() const;
    QString forwardingState() const;
    QString error() const;
    QVariantMap diagnostics() const;
    /** 整组验证并原子保存；失败不改变配置或当前连接，成功后断开旧串口，须手动连接。 */
    Q_INVOKABLE bool configure(const QVariantMap& configuration);
    /** 枚举当前设备；不自动选择设备，不打开串口，不改变已保存配置。 */
    Q_INVOKABLE void refreshPorts();
    /** 仅比赛页可异步连接；true 表示启动线程，驱动拒绝通过 state/error 报告。 */
    Q_INVOKABLE bool start();
    /** 关闭串口并清除待转发帧，可重复调用；不会断开独立 MQTT/视频。 */
    Q_INVOKABLE void stop();
    /** 会话导航入口；离开比赛页立即 stop，进入不自动连接。 */
    void setArena(bool is_arena);

   signals:
    /** 参数已完整保存，界面可重新读取 configuration。 */
    void configurationChanged();
    /** 端口列表已刷新；消失的已保存设备不会被另一设备替代。 */
    void portsChanged();
    /** 串口、转发或错误状态改变，不逐帧发射。 */
    void statusChanged();
    /** GUI 诊断快照改变，不携带原始控制字节。 */
    void diagnosticsChanged();

   private:
    void poll();
    void discardPending();
    void setError(const QString& error);
    bool load();

    DataPipeline& pipeline_;
    QString config_path_;
    QVariantMap configuration_;
    QVariantList ports_;
    QThread thread_;
    QObject* worker_ = nullptr;  ///< 线程结束在工作线程 deleteLater；GUI 只排队调用 stop。
    std::shared_ptr<SerialMailbox> mailbox_;
    QTimer poll_timer_;           ///< 14 ms 精确定时器；周期取最新值，不追赶补帧。
    QTimer cooldown_timer_;       ///< 单次精确定时器；待发布最新值等待本地发送冷却结束。
    qint64 next_publish_us_ = 0;  ///< GUI收到本地发送完成后至少13334µs才允许下一次提交。
    QByteArray pending_;          ///< GUI 唯一最新负载；空负载也合法，由时间戳区分空槽。
    qint64 pending_at_us_ = -1;
    qint64 forwarding_floor_us_ = 0;
    qint64 last_diagnostics_us_ = 0;
    quint64 request_id_ = 0;  ///< 最多一个本地发布在途，避免 GUI 卡顿产生控制队列。
    quint64 submitted_ = 0;
    quint64 sent_ = 0;
    quint64 failed_ = 0;
    quint64 offline_drops_ = 0;
    quint64 stale_drops_ = 0;
    quint64 gui_coalesced_ = 0;  ///< 已有 MQTT 在途时 GUI 最新槽被覆盖的次数。
    QVariantMap diagnostics_;
    QString state_ = "disconnected";
    QString forwarding_state_ = "未启用";
    QString error_;
    QString last_result_;
    bool is_arena_ = false;
    bool is_config_valid_ = true;  ///< 损坏/未来版本配置不静默覆盖。
};
