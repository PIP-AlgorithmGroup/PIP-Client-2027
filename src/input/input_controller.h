#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>

#include "communication/data_pipeline.h"
#include "platform/native_input.h"

/** GUI 输入入口；拥有原生适配器，采集/发送在独立线程，GUI 只以 30Hz 读取诊断。
 * pipeline 必须比本对象长寿；window 为借用，surface 销毁前同步停止并 join 原生线程。
 */
class InputController : public QObject {
    Q_OBJECT
    /// 当前完整配置副本；灵敏度倍数 0.05..10，频率为 75 或 200Hz。
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    /// 最近一秒实际交付率/间隔/全键状态；不代表硬件扫描率或远端执行确认。
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    /// 是否已获得原生锁定且输入健康，不等于 MQTT/机器人执行确认。
    Q_PROPERTY(bool captured READ captured NOTIFY diagnosticsChanged)
    /// 本地测试意图；测试状态永不产生机器人控制。
    Q_PROPERTY(bool testing READ testing NOTIFY diagnosticsChanged)

   public:
    /** 所属 GUI 线程构造；空路径使用应用配置目录的 input.json。 */
    explicit InputController(DataPipeline& pipeline, const QString& path = {},
                             QObject* parent = nullptr);
    /** 同步停止输入线程后撤销 MQTT 来源；不销毁外部窗口或 pipeline。 */
    ~InputController() override;
    /** GUI 快照 getter，不访问原生句柄。 */
    QVariantMap configuration() const;
    QVariantMap diagnostics() const;
    bool captured() const;
    bool testing() const;
    /** QML 组件就绪后绑定唯一窗口；更换窗口先 join 旧线程。 */
    Q_INVOKABLE void bindWindow(QWindow* window);
    /** 页面资格；总开关开启且主图传无遮挡才持续采集，不要求图像/MQTT，test_page 优先本地测试。 */
    Q_INVOKABLE void setContext(bool main_view, bool blocked, bool test_page);
    /** 原子验证并保存全部设置；失败保持旧配置，错误见诊断。200Hz 为联调选项。 */
    Q_INVOKABLE bool configure(double sensitivity, bool invert_y, int frequency);
    /** GUI 线程立即切换本场输出，不写盘；启动和离开比赛时关闭，测试页始终不发送。 */
    Q_INVOKABLE void setEnabled(bool enabled);
    /** 仅测试页可启动无限相对移动测试；Escape/失焦/离页立即退出锁定。 */
    Q_INVOKABLE void setTestLocked(bool locked);
    /** 清空报告间隔和计数，不改变按住键或网络资格。 */
    Q_INVOKABLE void resetStatistics();

   signals:
    /** 参数保存成功或本场输出开关变化；接收者须重新读取完整快照。 */
    void configurationChanged();
    /** 30Hz 合并的只读诊断更新。 */
    void diagnosticsChanged();

   protected:
    /** GUI surface/焦点边界；锁定意图期间隔离 Qt 普通键鼠，Esc 保留菜单退出通路。 */
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void updateIntent();
    void refresh();
    void stopNative();
    bool saveConfiguration(double sensitivity, bool invert_y, int frequency);
    DataPipeline& pipeline_;
    QString path_, config_error_;
    std::shared_ptr<InputState> state_ = std::make_shared<InputState>();
    std::unique_ptr<NativeInput> native_;
    QPointer<QWindow> window_;
    QTimer timer_;
    QVariantMap diagnostics_;
    double sensitivity_ = 1;
    bool invert_y_ = false, main_view_ = false, blocked_ = true, test_page_ = false;
    bool test_locked_ = false, captured_ = false;
    bool enabled_ = false;  ///< 本场输出意图，不持久化；不依赖图像、MQTT 或测试页资格。
    int frequency_ = 200;   ///< 用户要求默认 200Hz；75Hz 官方基线可在菜单选择，实际接纳须联调。
};
