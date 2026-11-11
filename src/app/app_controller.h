#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

/**
 * 欢迎页本地状态与偏好；不拥有数据链路、不建立网络、不推导比赛配置。
 * 全部方法和信号仅在所属 GUI 线程使用。由应用持有，生命周期长于 QML engine。
 */
class AppController : public QObject {
    Q_OBJECT
    /// 初始化状态 loading/ready/error；error 可重试或重置本地偏好。
    Q_PROPERTY(QString startupState READ startupState NOTIFY stateChanged)
    /// 当前本地页面 welcome/arena；arena 仅为空容器，不表示比赛会话已连接。
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY stateChanged)
    /// 空字符串表示未选择，仅接受 RMUL 或 RMUC。
    Q_PROPERTY(QString selectedMode READ selectedMode NOTIFY preferencesChanged)
    /// 进入请求已排队时为 true，用于阻止重复提交。
    Q_PROPERTY(bool isEntering READ isEntering NOTIFY stateChanged)
    /// 循环背景动效许可；默认关闭，实际运行还受窗口可见性约束。
    Q_PROPERTY(bool motionEnabled READ motionEnabled NOTIFY preferencesChanged)
    /// 用户的窗口/全屏偏好；默认窗口模式。
    Q_PROPERTY(bool fullScreen READ fullScreen NOTIFY fullScreenChanged)
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
    QString uiSize() const;
    double uiScale() const;
    QString lastError() const;
    QVariantList diagnostics() const;
    QString version() const;
    /** 返回打包声明，读取失败的项目明确标注，不显示个人文件路径。 */
    QString resourceNotices() const;

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
    /** 本地配置错误改变，可由界面非阻塞显示。 */
    void errorChanged();

   private:
    bool savePreferences(const QString& mode, bool motion, bool full_screen,
                         const QString& ui_size);
    void setError(const QString& error);

    QString settings_path_;  ///< 只在本地使用，不显示到 UI 或日志中。
    QString startup_state_ = "loading";
    QString current_page_ = "welcome";
    QString selected_mode_;
    QString ui_size_ = "medium";
    QString last_error_;
    bool motion_enabled_ = false;
    bool full_screen_ = false;
    bool is_entering_ = false;
    quint64 navigation_generation_ = 0;  ///< 隔离返回后仍在队列中的进入回调。
};
