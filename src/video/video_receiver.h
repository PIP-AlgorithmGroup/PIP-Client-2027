#pragma once

#include <QtQml/qqmlregistration.h>

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QVariantMap>
#include <memory>

#include "hevc_decoder.h"

/**
 * GUI 线程持有的图传会话。网络、组帧及解码在独立线程；单槽邮箱最多一张待显示图。
 * 进入 RMUL 时 start，离开/析构时同步 stop；worker 不访问任何 QML 对象。
 */
class VideoReceiver : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    /// 数字 IP 监听地址，默认所有 IPv4 接口 0.0.0.0；不作 DNS 或 multicast join。
    Q_PROPERTY(QString bindAddress READ bindAddress NOTIFY configurationChanged)
    /// UDP 端口 1..65535，官方默认 3334。
    Q_PROPERTY(int port READ port NOTIFY configurationChanged)
    /// 工程兼容项 0/1，默认 0；官方未明确，必须与实际发送端相符。
    Q_PROPERTY(int indexBase READ indexBase NOTIFY configurationChanged)
    /// true 表示接收会话未停止，可能仍在等待首帧；不代表已接收到图像。
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    /// 图传占位与设置面板的状态及错误；诊断数值通过独立只读属性提供。
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    /// 当前锁定 UDP 来源的数字 IP 和源端口；未知/停止/停包 1 秒后分别为空/0。
    /// 来自可进入组帧的包，不代表已解码或已认证；只读、不保存，不作为远端配置。
    Q_PROPERTY(QString sourceAddress READ sourceAddress NOTIFY sourceChanged)
    Q_PROPERTY(int sourcePort READ sourcePort NOTIFY sourceChanged)
    /// 每 250ms 发布；1 秒速率、10 秒收帧 low，-1 表示不可测/样本不足。非端到端延迟。
    /// decoder/decoderDevice/decoderFallback 为选择快照，hardwareActive 需真实硬件帧输出。
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)

   public:
    /** config_path 为空使用 AppConfigLocation/video.ini；无效文件整体回退且禁止启动。 */
    explicit VideoReceiver(const QString& config_path = {}, QObject* parent = nullptr);
    /** 中断并 join 接收线程后才释放邮箱；GUI 需先销毁 QML。 */
    ~VideoReceiver() override;
    /** 以下 getter 只在 GUI 线程调用，不触发 I/O；frame 返回只读共享图像。 */
    QString bindAddress() const;
    int port() const;
    int indexBase() const;
    bool running() const;
    QString status() const;
    QString error() const;
    QString sourceAddress() const;
    int sourcePort() const;
    VideoFrame frame() const;
    /** GUI 独有只读统计副本；不触发工作线程 I/O。 */
    QVariantMap diagnostics() const;
    /** 验证并原子保存整组配置；失败保留原配置/会话，成功则重启现有会话。 */
    Q_INVOKABLE bool configure(const QString& address, int port, int index_base);
    /** 异步启动绑定；重复调用幂等，失败通过 status/error 报告。 */
    Q_INVOKABLE void start();
    /** 最多等待当前数据报/解码完成，关闭 socket、释放解码器并立即清屏。 */
    Q_INVOKABLE void stop();

   signals:
    /** 完整配置已保存并应用。 */
    void configurationChanged();
    /** 会话状态/错误改变，仅 GUI 线程发射。 */
    void statusChanged();
    /** 当前来源获得、超时或清空；仅 GUI 线程发射，不按数据包重复通知。 */
    void sourceChanged();
    /** 最新帧改变或清屏；合并唤醒 GUI 取最新邮箱，无逐帧事件堆积。 */
    void frameChanged();
    /** 汇总统计更新或停止清空，仅 GUI 线程发射。 */
    void diagnosticsChanged();

   private:
    void receive(const QString& address, int port, int index_base, quint64 generation);
    // 工作线程持 mutex_ 调用；队列中最多一个本代唤醒，不捕获视频帧。
    void requestPollLocked(quint64 generation);
    void poll();
    QString config_path_;
    QString address_ = "0.0.0.0";
    int port_ = 3334;
    int index_base_ = 0;
    bool config_valid_ = true;
    bool running_ = false;
    QString status_ = "已停止";
    QString error_;
    QString source_address_;       ///< GUI 独有的当前来源快照；空字符串表示尚未锁定。
    int source_port_ = 0;          ///< GUI 独有的 UDP 源端口；0 表示尚未锁定。
    QString configuration_error_;  ///< GUI 独有；保留保存失败，不能被下一次邮箱通知清掉。
    VideoFrame frame_;
    std::unique_ptr<QThread> worker_;
    quint64 generation_ = 0;   ///< GUI 独有会话代数；阻止停止前的唤醒修改新会话。
    QVariantMap diagnostics_;  ///< GUI 线程独有；与视频单槽邮箱同一代会话。
    // 仅邮箱在 GUI 与接收线程间共享；图像发布后不再修改，所有访问必须持锁。
    QMutex mutex_;
    VideoFrame pending_frame_;
    quint64 revision_ = 0;
    quint64 displayed_revision_ = 0;
    QString pending_error_;
    QString pending_status_;
    QString pending_source_address_;  ///< 受 mutex_ 保护，来源转换时更新，不按每包复制。
    int pending_source_port_ = 0;     ///< 与 pending_source_address_ 同锁发布和读取。
    bool failed_ = false;
    QVariantMap pending_diagnostics_;  ///< 工作线程每 250ms 发布值副本，受 mutex_ 保护。
    bool poll_pending_ = false;        ///< 受 mutex_ 保护；排队到 GUI 的合并唤醒是否尚未处理。
};
