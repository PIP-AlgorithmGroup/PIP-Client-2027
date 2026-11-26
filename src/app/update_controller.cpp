#include "update_controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <algorithm>

namespace {
const QString kRepository = "https://github.com/PIP-AlgorithmGroup/PIP-Client-2027";
const QString kApi = "https://api.github.com/repos/PIP-AlgorithmGroup/PIP-Client-2027/releases";

bool trustedUrl(const QUrl& url) {
    return url.scheme() == "https" && url.userInfo().isEmpty() && url.port(443) == 443 &&
           QStringList{"api.github.com", "github.com", "release-assets.githubusercontent.com",
                       "objects.githubusercontent.com"}
               .contains(url.host());
}
QString platformSuffix() {
    if (QSysInfo::currentCpuArchitecture() != "x86_64") return {};
#ifdef Q_OS_WIN
    return "Windows-x86_64-Setup.exe";
#else
    return "Ubuntu-amd64.deb";
#endif
}
QString helperName() {
#ifdef Q_OS_WIN
    return "PIPUpdateHelper.exe";
#else
    return "PIPUpdateHelper";
#endif
}
}  // namespace

UpdateController::UpdateController(const Options& options, QObject* parent)
    : QObject(parent), options_(options) {
    if (options_.current_version.isEmpty())
        options_.current_version = QCoreApplication::applicationVersion();
    if (options_.platform_suffix.isEmpty()) options_.platform_suffix = platformSuffix();
    if (options_.cache_directory.isEmpty())
        options_.cache_directory =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
    if (options_.settings_path.isEmpty())
        options_.settings_path =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/updates.ini";
    if (options_.test_origin.scheme() != "http" || options_.test_origin.host() != "127.0.0.1" ||
        !options_.test_origin.userInfo().isEmpty())
        options_.test_origin = QUrl{};
    QSettings settings(options_.settings_path, QSettings::IniFormat);
    install_supported_ = supportsInstallation();
    include_prerelease_ =
        settings.value("include_prerelease", options_.current_version.contains('-')).toBool();
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { fail("检查或下载超时，请重试。"); });
    // 助手只写固定结果文件；失败/取消时重启旧版，下次启动可见原因。
    QFile result(options_.cache_directory + "/install-result.txt");
    if (result.open(QIODevice::ReadOnly)) {
        const auto previous = result.read(256).trimmed();
        if (previous == "success")
            status_ = "上次更新安装完成，可再次检查版本。";
        else
            status_ = "上次更新未完成或被取消，当前版本仍可使用，请重新检查更新。";
        result.close();
        result.remove();
    }
}

UpdateController::~UpdateController() {
    stopReply();
    if (verify_thread_) verify_thread_->wait();
    file_.reset();
    if (!handed_off_)
        for (const auto& directory : staging_directories_) QDir(directory).removeRecursively();
}
QString UpdateController::state() const { return state_; }
QString UpdateController::status() const { return status_; }
QString UpdateController::availableVersion() const { return release_.version; }
QString UpdateController::releaseNotes() const { return release_.notes; }
double UpdateController::progress() const {
    return release_.size > 0 ? std::clamp(double(received_) / release_.size, 0.0, 1.0) : 0;
}
bool UpdateController::includePrerelease() const { return include_prerelease_; }
bool UpdateController::busy() const {
    return state_ == "checking" || state_ == "downloading" || state_ == "verifying";
}
bool UpdateController::canInstall() const {
    return state_ == "ready" && install_allowed_ && install_supported_;
}
bool UpdateController::canDownload() const {
    return !busy() && state_ != "ready" && !release_.download_url.isEmpty() &&
           release_.sha256.size() == 32;
}
QString UpdateController::installHint() const {
    if (!install_allowed_) return "请返回欢迎页后安装更新。";
    if (!install_supported_) return "当前为便携版或开发构建，请打开下载位置后手动安装。";
#ifdef Q_OS_LINUX
    return "安装时由系统请求授权；完成后自动重新启动客户端。";
#else
    return "安装时将退出客户端，完成后自动重新启动。";
#endif
}
void UpdateController::setState(const QString& state, const QString& status) {
    state_ = state;
    status_ = status;
    emit changed();
}
void UpdateController::stopReply() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_.clear();
    }
}
void UpdateController::fail(const QString& message) {
    deadline_.stop();
    stopReply();
    file_.reset();
    hash_.reset();
    setState("error", message);
}
void UpdateController::setIncludePrerelease(bool enabled) {
    if (busy() || include_prerelease_ == enabled) return;
    QSettings settings(options_.settings_path, QSettings::IniFormat);
    settings.setValue("include_prerelease", enabled);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        fail("无法保存更新渠道，请检查配置目录权限。");
        return;
    }
    include_prerelease_ = enabled;
    release_ = {};
    package_path_.clear();
    setState("idle", "更新渠道已切换，请重新检查。");
}
void UpdateController::setInstallAllowed(bool allowed) {
    if (install_allowed_ == allowed) return;
    install_allowed_ = allowed;
    emit changed();
}
void UpdateController::check() {
    if (busy()) return;
    if (options_.platform_suffix.isEmpty()) {
        fail("当前系统或架构没有可用的自动更新安装包。");
        return;
    }
    release_ = {};
    package_path_.clear();
    releases_ = {};
    received_ = 0;
    page_ = 1;
    setState("checking", "正在检查 GitHub 发行版本…");
    deadline_.start(60000);
    fetchPage();
}
void UpdateController::fetchPage() {
    request(QUrl(kApi + "?per_page=100&page=" + QString::number(page_)), false);
}
QUrl UpdateController::transportUrl(const QUrl& url) const {
    if (options_.test_origin.isEmpty()) return url;
    QUrl local = options_.test_origin;
    local.setPath(url.path());
    local.setQuery(url.query());
    return local;
}
void UpdateController::request(const QUrl& url, bool is_package, int redirects) {
    if (!trustedUrl(url) &&
        !(url.scheme() == "http" && !options_.test_origin.isEmpty() && url.host() == "127.0.0.1" &&
          url.port() == options_.test_origin.port())) {
        fail("更新服务返回了不受信任的下载地址。");
        return;
    }
    response_.clear();
    QNetworkRequest network_request(transportUrl(url));
    network_request.setRawHeader("User-Agent", "PIPClient/" + options_.current_version.toUtf8());
    network_request.setRawHeader(
        "Accept", is_package ? "application/octet-stream" : "application/vnd.github+json");
    network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
    network_request.setTransferTimeout(options_.timeout_ms);
    auto* reply = network_.get(network_request);
    reply_ = reply;
    reply->setReadBufferSize(256 * 1024);
    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply, is_package] { consume(reply, is_package); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, is_package, redirects] {
        if (reply_ != reply) return;
        const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http >= 300 && http < 400) {
            const auto next = reply->url().resolved(
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            reply_.clear();
            reply->deleteLater();
            if (redirects >= 5)
                fail("更新服务重定向次数过多。");
            else
                request(next, is_package, redirects + 1);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || http != 200) {
            fail(http == 403 || http == 429 ? "更新服务请求受限，请稍后重试。"
                                            : "无法连接更新服务或下载失败，请检查网络后重试。");
            return;
        }
        consume(reply, is_package);
        if (reply_ != reply) return;
        reply_.clear();
        reply->deleteLater();
        if (is_package) {
            if (received_ != release_.size || hash_->result() != release_.sha256) {
                fail("安装包大小或 SHA-256 校验失败，文件已丢弃，请重新下载。");
                return;
            }
            if (!file_->commit()) {
                fail("安装包无法保存，请检查磁盘空间与权限。");
                return;
            }
            file_.reset();
            hash_.reset();
            // 完整包保留在用户缓存，方便便携/开发构建在关闭客户端后手动安装。
            staging_directories_.removeAll(QFileInfo(package_path_).absolutePath());
            deadline_.stop();
            setState("ready", "下载完成，安装包校验通过。");
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(response_, &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            fail("更新服务返回的发行信息格式不正确。");
            return;
        }
        const auto page = document.array();
        for (const auto& value : page) releases_.append(value);
        if (page.size() == 100) {
            if (++page_ > 10)
                fail("发行列表过长，请前往官方发行页查看。");
            else
                fetchPage();
            return;
        }
        deadline_.stop();
        const auto selection = selectUpdateRelease(releases_, options_.current_version,
                                                   include_prerelease_, options_.platform_suffix);
        release_ = selection.release;
        if (!selection.error.isEmpty())
            fail(selection.error);
        else if (selection.has_update)
            setState("available", "发现新版 v" + release_.version);
        else
            setState("current", "当前渠道没有比 v" + options_.current_version + " 更新的版本。");
    });
}
void UpdateController::consume(QNetworkReply* reply, bool is_package) {
    // 重定向/错误响应的正文不能进入包文件或摘要；每次只读有界网络缓冲。
    if (reply_ != reply ||
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        reply->readAll();
        return;
    }
    const auto bytes = reply->readAll();
    if (is_package) {
        if (!file_ || received_ + bytes.size() > release_.size ||
            file_->write(bytes) != bytes.size()) {
            fail("安装包大小异常或磁盘写入失败，部分文件已丢弃。");
            return;
        }
        hash_->addData(bytes);
        received_ += bytes.size();
        emit changed();
    } else {
        if (response_.size() + bytes.size() > 2 * 1024 * 1024) {
            fail("更新服务返回的信息超过允许大小。");
            return;
        }
        response_ += bytes;
    }
}
void UpdateController::download() {
    if (!canDownload()) return;
    if (!QDir().mkpath(options_.cache_directory)) {
        fail("无法创建更新下载目录。");
        return;
    }
    QTemporaryDir directory(options_.cache_directory + "/package-XXXXXX");
    if (!directory.isValid()) {
        fail("无法创建安装包临时目录。");
        return;
    }
    package_path_ = directory.filePath(release_.file_name);
    file_ = std::make_unique<QSaveFile>(package_path_);
    file_->setDirectWriteFallback(false);
    if (!file_->open(QIODevice::WriteOnly)) {
        fail("安装包下载文件无法写入。");
        return;
    }
    directory.setAutoRemove(false);
    staging_directories_.append(directory.path());
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    received_ = 0;
    setState("downloading", "正在下载安装包…");
    deadline_.start(10 * 60 * 1000);
    request(release_.download_url, true);
}
void UpdateController::cancel() {
    if (!busy()) return;
    ++generation_;
    deadline_.stop();
    stopReply();
    file_.reset();
    hash_.reset();
    setState(package_path_.isEmpty() || !QFileInfo::exists(package_path_) ? "idle" : "ready",
             "已取消，不影响当前版本使用。");
}
bool UpdateController::supportsInstallation() const {
    const auto executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    if (!QFileInfo::exists(QCoreApplication::applicationDirPath() + "/" + helperName()))
        return false;
#ifdef Q_OS_WIN
    QSettings registration(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
        "{F6938828-738D-428E-8B53-EEB02EA127F9}_is1",
        QSettings::NativeFormat);
    const auto root = registration.value("InstallLocation").toString();
    return !root.isEmpty() &&
           executable.compare(QFileInfo(root + "/bin/PIPClient.exe").canonicalFilePath(),
                              Qt::CaseInsensitive) == 0;
#else
    return executable == "/opt/pip-client/bin/PIPClient" && QFileInfo::exists("/usr/bin/pkexec");
#endif
}
void UpdateController::install() {
    if (!canInstall() || (verify_thread_ && verify_thread_->isRunning())) return;
    struct Result {
        bool valid = false;
    };
    auto result = std::make_shared<Result>();
    const auto path = package_path_;
    const auto release = release_;
    const auto generation = ++generation_;
    setState("verifying", "正在复核安装包，完成后退出并安装…");
    verify_thread_ = std::unique_ptr<QThread>(QThread::create([path, release, result] {
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (file.open(QIODevice::ReadOnly) && file.size() == release.size && hash.addData(&file))
            result->valid = hash.result() == release.sha256;
    }));
    connect(verify_thread_.get(), &QThread::finished, this, [this, result, generation] {
        if (generation != generation_) return;
        if (!result->valid)
            fail("安装前复核失败，安装包可能已被修改，请重新下载。");
        else if (!install_allowed_ || !supportsInstallation())
            setState("ready", "当前无法安装，请返回欢迎页后重试。");
        else
            handOff();
    });
    verify_thread_->start();
}
void UpdateController::handOff() {
    // 助手必须复制到更新目录；Windows 不能从待升级目录加载并锁住助手本身。
    const auto helper = QFileInfo(package_path_).dir().filePath(helperName());
    if (!QFile::copy(QCoreApplication::applicationDirPath() + "/" + helperName(), helper)) {
        fail("无法准备更新助手，请重新下载后重试。");
        return;
    }
    QFile::setPermissions(helper,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QStringList arguments{QString::number(QCoreApplication::applicationPid()), package_path_,
                                QCoreApplication::applicationFilePath(),
                                options_.cache_directory + "/install-result.txt"};
    QProcess process;
    process.setProgram(helper);
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(helper).absolutePath());
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= 0x08000000;  // CREATE_NO_WINDOW：助手不弹出控制台。
    });
#endif
    if (!process.startDetached()) {
        fail("更新助手无法启动，客户端继续运行。");
        return;
    }
    deadline_.stop();
    handed_off_ = true;
    setState("verifying", "正在退出客户端并安装更新…");
    emit quitForUpdate();
}
void UpdateController::openDownloadDirectory() {
    if (package_path_.isEmpty() || !QFileInfo::exists(package_path_) ||
        !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(package_path_).absolutePath())))
        setState(state_, "无法打开下载位置，请重新下载安装包。");
}
void UpdateController::openReleasePage() {
    if (!QDesktopServices::openUrl(release_.page_url.isEmpty() ? QUrl(kRepository + "/releases")
                                                               : release_.page_url))
        setState(state_, "无法打开浏览器，请稍后重试。");
}
