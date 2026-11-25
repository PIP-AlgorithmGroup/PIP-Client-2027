#pragma once

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <memory>

#include "update_release.h"

/** 手动更新协调器。全部接口/信号只在所属 GUI 线程调用，构造时不联网。
 * 网络异步、有界缓存；安装前后台复核文件，析构取消网络并等待本对象的校验线程。
 * 所有者须在本对象之后销毁 Qt 应用；安装仅交给外部助手，不在本进程覆盖文件。
 */
class UpdateController : public QObject {
    Q_OBJECT
    /// idle/checking/current/available/downloading/ready/verifying/error；错误可重试。
    Q_PROPERTY(QString state READ state NOTIFY changed)
    /// 面向用户的当前进度或错误，不把网络错误解释成“已是最新版”。
    Q_PROPERTY(QString status READ status NOTIFY changed)
    /// 已选新版与纯文本发行说明；未发现新版时为空。
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    /// 字节下载进度 0..1，不含校验/安装完成含义。
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    /// 当前版本含预发布后缀时默认开启；显式选择保存在独立本地偏好中。
    Q_PROPERTY(
        bool includePrerelease READ includePrerelease WRITE setIncludePrerelease NOTIFY changed)
    /// 检查、下载或复核中为 true；禁止重复请求及切换渠道。
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// 仅安装版、欢迎页及已验证下载允许安装；安装前仍重新验证。
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY changed)
    Q_PROPERTY(QString installHint READ installHint NOTIFY changed)

   public:
    /** 可注入离线测试路径和环回 HTTP 服务。生产使用默认选项，不接受环境变量改写更新源。
     * test_origin 只允许 http://127.0.0.1:<port>，用于保持真实网络/取消/摘要路径可测。
     */
    struct Options {
        QString current_version, settings_path, cache_directory, platform_suffix;
        QUrl test_origin;
        int timeout_ms = 15000;  ///< 单请求无活动超时；整次检查/下载另有总时限。
    };
    explicit UpdateController(const Options& options, QObject* parent = nullptr);
    ~UpdateController() override;
    /** GUI 线程快照 getter，不联网、不启动安装器。 */
    QString state() const;
    QString status() const;
    QString availableVersion() const;
    QString releaseNotes() const;
    double progress() const;
    bool includePrerelease() const;
    bool busy() const;
    bool canInstall() const;
    bool canDownload() const;
    QString installHint() const;
    /** 忙时拒绝；保存失败保留原渠道并报告错误，切换后清除旧渠道候选。 */
    void setIncludePrerelease(bool enabled);
    /** 由导航所有者同步授权；离开欢迎页立即撤销，校验在途时也必须重新检查。 */
    void setInstallAllowed(bool allowed);
    /** 仅用户操作触发，异步读取公开发行列表；忙时忽略。 */
    Q_INVOKABLE void check();
    /** 仅 available 或下载失败后调用；按固定平台选择下载，无后台自动安装。 */
    Q_INVOKABLE void download();
    /** 取消检查/下载并丢弃部分文件；磁盘复核不能强行终止，结果被作废。 */
    Q_INVOKABLE void cancel();
    /** 安装前后台复核摘要与大小；助手启动成功才请求退出。拒绝时保留客户端运行。 */
    Q_INVOKABLE void install();
    /** 打开已完成下载的本机目录/固定官方发行页；失败给出可见状态。 */
    Q_INVOKABLE void openDownloadDirectory();
    Q_INVOKABLE void openReleasePage();

   signals:
    /** 所有用户可见快照变化，仅所属 GUI 线程发射。 */
    void changed();
    /** 已启动等待本进程退出的安装助手；宿主须正常退出并释放全部会话。 */
    void quitForUpdate();

   private:
    void setState(const QString& state, const QString& status);
    void fail(const QString& message);
    void stopReply();
    void request(const QUrl& url, bool is_package, int redirects = 0);
    void consume(QNetworkReply* reply, bool is_package);
    void fetchPage();
    void handOff();
    bool supportsInstallation() const;
    QUrl transportUrl(const QUrl& url) const;

    Options options_;
    QNetworkAccessManager network_;  ///< 所属 GUI 线程使用，reply 由 Qt 管理并 deleteLater。
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;  ///< 检查 60 秒、下载 10 分钟总时限；不随重定向重置。
    QJsonArray releases_;
    QByteArray response_;
    UpdateRelease release_;
    std::unique_ptr<QSaveFile> file_;  ///< 仅写入本次专有缓存目录，取消不留下半包。
    std::unique_ptr<QCryptographicHash> hash_;
    std::unique_ptr<QThread> verify_thread_;  ///< 最多一个后台文件复核，析构时 join。
    QString state_ = "idle", status_ = "点击检查更新，仅在你操作时访问 GitHub。";
    QString package_path_;
    QStringList staging_directories_;  ///< 只清理本对象创建的临时目录，交接成功后由下次维护处理。
    qint64 received_ = 0;
    int page_ = 1;
    quint64 generation_ = 0;  ///< GUI 独有；取消后丢弃旧复核回调，不恢复安装资格。
    bool include_prerelease_ = false;
    bool install_allowed_ = true;
    bool install_supported_ = false;  ///< 启动时冻结，界面 getter 不反复读取注册表/文件系统。
    bool handed_off_ = false;         ///< 外部助手仍使用安装包，退出时必须保留对应目录。
};
