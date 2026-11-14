#pragma once

#include <QByteArray>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DataPipeline;
class CustomControlChannel;
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
    std::shared_ptr<CustomControlChannel> channel_;  ///< GUI创建/取消，串口线程持同一通道提交。
    QTimer poll_timer_;              ///< 仅同步状态与聚合计数，不参与数据交付、限频或完成反馈。
    QVariantMap forwarding_totals_;  ///< 本次串口连接内已失效通道的累计统计，仅GUI访问。
    qint64 last_diagnostics_us_ = 0;
    QVariantMap diagnostics_;
    QString state_ = "disconnected";
    QString forwarding_state_ = "未启用";
    QString error_;
    bool is_arena_ = false;
    bool is_config_valid_ = true;  ///< 损坏/未来版本配置不静默覆盖。
};
