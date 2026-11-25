#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "app/update_controller.h"

/**
 * 欢迎页本地状态与偏好；不拥有比赛数据链路、不推导比赛配置。
 * 独立更新服务仅响应用户显式联网请求，导航同步约束其安装资格。
 * 全部方法和信号仅在所属 GUI 线程使用。由应用持有，生命周期长于 QML engine。
 */
class AppController : public QObject {
    Q_OBJECT
    /// 初始化状态 loading/ready/error；error 可重试或重置本地偏好。
    Q_PROPERTY(QString startupState READ startupState NOTIFY stateChanged)
    /// 当前页面 welcome/arena；页面自身管理视频监听，不表示 MQTT 比赛会话已连接。
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY stateChanged)
    /// 空字符串表示未选择，仅接受 RMUL 或 RMUC。
    Q_PROPERTY(QString selectedMode READ selectedMode NOTIFY preferencesChanged)
    /// 进入请求已排队时为 true，用于阻止重复提交。
    Q_PROPERTY(bool isEntering READ isEntering NOTIFY stateChanged)
    /// 循环背景动效许可；默认关闭，实际运行还受窗口可见性约束。
    Q_PROPERTY(bool motionEnabled READ motionEnabled NOTIFY preferencesChanged)
    /// 用户的窗口/全屏偏好；默认窗口模式。
    Q_PROPERTY(bool fullScreen READ fullScreen NOTIFY fullScreenChanged)
    /// 已保存的垂直同步偏好；默认开启，修改后下次启动应用，不重建运行中窗口。
    Q_PROPERTY(bool verticalSync READ verticalSync NOTIFY verticalSyncChanged)
    /// 本次启动向 Qt 提交的同步请求；不代表驱动或合成器实际遵从。
    Q_PROPERTY(bool verticalSyncRequested READ verticalSyncRequested NOTIFY verticalSyncChanged)
    /// 已保存偏好与本次启动快照不同；改回原值即取消待重启状态。
    Q_PROPERTY(bool verticalSyncRestartRequired READ verticalSyncRestartRequired NOTIFY
                   verticalSyncChanged)
    /// 当前请求、环境覆盖及待重启提示；不得用它推断物理扫描模式。
    Q_PROPERTY(QString verticalSyncStatus READ verticalSyncStatus NOTIFY verticalSyncChanged)
    /// UI 档位 small/medium/large，默认 medium；独立于窗口逻辑尺寸与系统 DPI。
    Q_PROPERTY(QString uiSize READ uiSize NOTIFY preferencesChanged)
    /// 档位倍率 0.9/1.0/1.1，仅用于 UI 字体与控件尺寸，不用于窗口尺寸。
    Q_PROPERTY(double uiScale READ uiScale NOTIFY preferencesChanged)
    /// 最近本地配置错误；不包含个人路径或凭证。
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    /// 已读取的系统信息；不将设备存在推断为加速能力可用。
    Q_PROPERTY(QVariantList diagnostics READ diagnostics CONSTANT)
    /// CMake 工程版本，与应用构建保持一致。
    Q_PROPERTY(QString version READ version CONSTANT)
    /// 打包资源来源与字体许可全文，按需读取，不访问网络。
    Q_PROPERTY(QString resourceNotices READ resourceNotices CONSTANT)
    /// 当前运行日志根目录，只用于用户本机收集，不写入网络配置。
    Q_PROPERTY(QString logDirectory READ logDirectory NOTIFY logLocationChanged)
    /// 日志初始化/写入失败时的可见降级说明，不影响视频或通信。
    Q_PROPERTY(QString loggingStatus READ loggingStatus NOTIFY logLocationChanged)
    /// 手动更新服务独立管理网络；启动不检查，导航同步撤销比赛页的安装资格。
    Q_PROPERTY(UpdateController* updater READ updater CONSTANT)

   public:
    /** settings_path 为空时使用 Qt AppConfigLocation；显式路径便于测试隔离。 */
    explicit AppController(const QString& settings_path = {}, QObject* parent = nullptr);

    /** 以下 getter 返回本地状态副本，不触发 I/O。 */
    QString startupState() const;
    QString currentPage() const;
    QString selectedMode() const;
    bool isEntering() const;
    bool motionEnabled() const;
    bool fullScreen() const;
    bool verticalSync() const;
    bool verticalSyncRequested() const;
    bool verticalSyncRestartRequired() const;
    QString verticalSyncStatus() const;
    QString uiSize() const;
    double uiScale() const;
    QString lastError() const;
    QVariantList diagnostics() const;
    QString version() const;
    /** 返回打包声明，读取失败的项目明确标注，不显示个人文件路径。 */
    QString resourceNotices() const;
    /** GUI 线程读取本机诊断位置与错误说明，不触发磁盘 I/O。 */
    QString logDirectory() const;
    QString loggingStatus() const;
    /** 借用本控制器持有的更新服务，仅 GUI 线程使用，不转移所有权。 */
    UpdateController* updater();
    /** 启动/低频诊断观察器注入日志根目录和写入错误；不拥有日志服务。 */
    void setLogLocation(const QString& directory, const QString& error);
    /** 由用户操作打开本机日志目录；未配置返回 false，不发送/上传任何文件。 */
    Q_INVOKABLE bool openLogDirectory();

    /** 完整验证配置后一次性应用；失败保留已应用值（首次为默认）并进入 error。 */
    Q_INVOKABLE void initialize();
    /** 仅在欢迎页就绪且未进入时选择；保存失败返回 false，不改变已应用状态。 */
    Q_INVOKABLE bool selectMode(const QString& mode);
    /** 排队创建本地容器；无选择、未就绪或重复请求返回 false，不发网络命令。 */
    Q_INVOKABLE bool enterArena();
    /** 返回欢迎页并取消尚未完成的本地进入请求；不修改选择。 */
    Q_INVOKABLE void returnToWelcome();
    /** 保存后应用动效许可；写失败返回 false 并保持原值。 */
    Q_INVOKABLE bool setMotionEnabled(bool enabled);
    /** 保存后应用全屏偏好；写失败返回 false 并保持原值。 */
    Q_INVOKABLE bool setFullScreen(bool enabled);
    /** 原子保存同步偏好；未就绪/写失败返回 false，不改变当前格式或窗口状态。 */
    Q_INVOKABLE bool setVerticalSync(bool enabled);
    /**
     * GUI 启动线程在 initialize 后、首个窗口创建前调用一次，设置 Qt 默认 swap interval。
     * 后续调用幂等；窗口已存在时拒绝应用并报告错误。保留 QSG_NO_VSYNC 外部覆盖，
     * 全部平台采用同一公开 Qt 请求，实际是否遵从由驱动/合成器决定。
     * 无 GUI 的单元测试仅验证默认格式，不创建图形资源。
     */
    void applyStartupGraphics();
    /** 保存并应用 UI 档位；非法值、未就绪或写入失败返回 false，保留原值。 */
    Q_INVOKABLE bool setUiSize(const QString& size);
    /** 清空偏好文件并写入默认值；成功恢复 ready，失败报告错误且不应用部分值。 */
    Q_INVOKABLE void resetPreferences();

   signals:
    /** 本地页面、初始化或进入状态改变。 */
    void stateChanged();
    /** 模式、动效或 UI 档位已完整应用；全屏使用独立通知，避免重置原生窗口状态。 */
    void preferencesChanged();
    /** 仅在全屏偏好实际改变时发射，模式与 UI 档位变更不得重置窗口状态。 */
    void fullScreenChanged();
    /** 同步偏好、启动请求或待重启说明改变；不通知导航、全屏或视频会话。 */
    void verticalSyncChanged();
    /** 本地配置错误改变，可由界面非阻塞显示。 */
    void errorChanged();
    /** 诊断目录或日志可用状态改变，仅 GUI 线程发射。 */
    void logLocationChanged();

   private:
    bool savePreferences(const QString& mode, bool motion, bool full_screen, const QString& ui_size,
                         bool vertical_sync);
    void setError(const QString& error);

    QString settings_path_;     ///< 只在本地使用，不显示到 UI 或日志中。
    UpdateController updater_;  ///< 长于 QML 生命周期；与比赛业务订阅分开联网。
    QString startup_state_ = "loading";
    QString current_page_ = "welcome";
    QString selected_mode_;
    QString ui_size_ = "medium";
    QString last_error_;
    QString log_directory_;
    QString logging_error_;
    bool motion_enabled_ = false;
    bool full_screen_ = false;
    bool vertical_sync_ = true;
    bool startup_vertical_sync_ = true;  ///< 首次应用时冻结；initialize/reset 不得改写此快照。
    bool vertical_sync_requested_ = true;
    bool has_graphics_applied_ = false;
    bool has_sync_override_ = false;  ///< Qt 检查变量存在性，"0" 和空值也可能请求关闭同步。
    bool is_entering_ = false;
    quint64 navigation_generation_ = 0;  ///< 隔离返回后仍在队列中的进入回调。
};
