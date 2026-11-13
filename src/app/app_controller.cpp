#include "app_controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

namespace {

bool isModeValid(const QString& mode) { return mode.isEmpty() || mode == "RMUL" || mode == "RMUC"; }

bool isUiSizeValid(const QString& size) {
    return size == "small" || size == "medium" || size == "large";
}

// QSettings 会把文本 INI 的布尔值读为字符串，必须精确校验，不能让 "maybe" 静默转成 true。
bool isBooleanValid(const QVariant& value) {
    return value.toString() == "true" || value.toString() == "false";
}

}  // namespace

AppController::AppController(const QString& settings_path, QObject* parent)
    : QObject(parent),
      settings_path_(settings_path.isEmpty()
                         ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                               .filePath("preferences.ini")
                         : settings_path) {}

QString AppController::startupState() const { return startup_state_; }
QString AppController::currentPage() const { return current_page_; }
QString AppController::selectedMode() const { return selected_mode_; }
bool AppController::isEntering() const { return is_entering_; }
bool AppController::motionEnabled() const { return motion_enabled_; }
bool AppController::fullScreen() const { return full_screen_; }
bool AppController::verticalSync() const { return vertical_sync_; }
bool AppController::verticalSyncRequested() const { return vertical_sync_requested_; }
bool AppController::verticalSyncRestartRequired() const {
    return has_graphics_applied_ && vertical_sync_ != startup_vertical_sync_;
}
QString AppController::verticalSyncStatus() const {
    if (!has_graphics_applied_) return "将在下次启动时应用。";
    QString status = QString("本次启动请求：%1。").arg(vertical_sync_requested_ ? "开启" : "关闭");
    if (has_sync_override_) status += "\n启动环境 QSG_NO_VSYNC 已请求关闭，优先于界面偏好。";
    if (verticalSyncRestartRequired()) status += "\n已保存，重启客户端后应用。";
    return status;
}

void AppController::applyStartupGraphics() {
    if (has_graphics_applied_) return;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) &&
        !QGuiApplication::allWindows().isEmpty()) {
        setError("窗口已经创建，垂直同步设置需要重启客户端后应用。");
        return;
    }
    startup_vertical_sync_ = vertical_sync_;
    // Qt 6.9 场景图缓存变量的存在性；不能用值 "0" 表示开启，也不在运行中改环境。
    has_sync_override_ = qEnvironmentVariableIsSet("QSG_NO_VSYNC");
    vertical_sync_requested_ = vertical_sync_ && !has_sync_override_;
    auto format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(vertical_sync_requested_ ? 1 : 0);
    QSurfaceFormat::setDefaultFormat(format);
    has_graphics_applied_ = true;
    emit verticalSyncChanged();
}
QString AppController::uiSize() const { return ui_size_; }
double AppController::uiScale() const {
    return ui_size_ == "small" ? 0.9 : ui_size_ == "large" ? 1.1 : 1.0;
}
QString AppController::lastError() const { return last_error_; }
QString AppController::version() const { return QCoreApplication::applicationVersion(); }
QString AppController::logDirectory() const { return log_directory_; }
QString AppController::loggingStatus() const {
    if (!logging_error_.isEmpty()) return "日志记录不可用：" + logging_error_;
    return log_directory_.isEmpty() ? "当前运行未启用文件日志。"
                                    : "本次运行日志已启用，退出与异常证据保存在本机。";
}
void AppController::setLogLocation(const QString& directory, const QString& error) {
    if (log_directory_ == directory && logging_error_ == error) return;
    log_directory_ = directory;
    logging_error_ = error;
    emit logLocationChanged();
}
bool AppController::openLogDirectory() {
    return !log_directory_.isEmpty() &&
           QDesktopServices::openUrl(QUrl::fromLocalFile(log_directory_));
}

QString AppController::resourceNotices() const {
    QStringList notices;
    for (const auto* path :
         {":/resources/welcome/notice.txt", ":/resources/fonts/ofl_barlow.txt",
          ":/resources/fonts/ofl_noto_sans_sc.txt", ":/resources/icons/license_lucide.txt"}) {
        QFile file(QString::fromLatin1(path));
        notices.append(file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                                      : QString("声明文件无法读取。"));
    }
    return notices.join("\n\n");
}

QVariantList AppController::diagnostics() const {
    return {
        QVariantMap{{"label", "系统"}, {"value", QSysInfo::prettyProductName()}},
        QVariantMap{{"label", "架构"}, {"value", QSysInfo::currentCpuArchitecture()}},
        QVariantMap{{"label", "Qt 版本"}, {"value", QString::fromLatin1(qVersion())}},
        QVariantMap{{"label", "窗口平台"}, {"value", QGuiApplication::platformName()}},
        QVariantMap{{"label", "模型推理"}, {"value", "尚未接入"}},
        QVariantMap{{"label", "视频"}, {"value", "RMUL UDP / HEVC 自动硬解及 CPU 回退"}},
        QVariantMap{{"label", "3D"}, {"value", "尚未接入"}},
    };
}

void AppController::setError(const QString& error) {
    if (last_error_ != error) {
        last_error_ = error;
        emit errorChanged();
    }
}

void AppController::initialize() {
    if (current_page_ != "welcome" || is_entering_) {
        return;
    }
    QSettings settings(settings_path_, QSettings::IniFormat);
    const auto mode = settings.value("mode", "").toString();
    const auto motion = settings.value("motion", false);
    const auto full_screen = settings.value("fullscreen", false);
    const auto vertical_sync = settings.value("vertical_sync", true);
    const auto ui_size = settings.value("ui_size", "medium").toString();
    const auto version = settings.value("version", 1).toString();
    // 全部字段校验通过才提交，避免损坏配置造成部分全屏/动效/模式偏好生效。
    if (settings.status() != QSettings::NoError || version != "1" || !isModeValid(mode) ||
        !isBooleanValid(motion) || !isBooleanValid(full_screen) || !isBooleanValid(vertical_sync) ||
        !isUiSizeValid(ui_size)) {
        startup_state_ = "error";
        setError("本地偏好无法读取或格式不正确，请重试或恢复默认设置。");
        emit stateChanged();
        return;
    }
    const bool has_full_screen_changed = full_screen_ != (full_screen.toString() == "true");
    selected_mode_ = mode;
    ui_size_ = ui_size;
    motion_enabled_ = motion.toString() == "true";
    full_screen_ = full_screen.toString() == "true";
    vertical_sync_ = vertical_sync.toString() == "true";
    startup_state_ = "ready";
    setError({});
    emit preferencesChanged();
    emit verticalSyncChanged();
    if (has_full_screen_changed) {
        emit fullScreenChanged();
    }
    emit stateChanged();
}

bool AppController::savePreferences(const QString& mode, bool motion, bool full_screen,
                                    const QString& ui_size, bool vertical_sync) {
    QSettings settings(settings_path_, QSettings::IniFormat);
    settings.setValue("version", 1);
    settings.setValue("mode", mode);
    settings.setValue("motion", motion);
    settings.setValue("fullscreen", full_screen);
    settings.setValue("ui_size", ui_size);
    settings.setValue("vertical_sync", vertical_sync);
    // 使用 QSettings 原子同步；失败不发布内存状态，界面仍可操作并显示错误。
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError("无法保存本地偏好，请检查配置目录写入权限。");
        return false;
    }
    setError({});
    return true;
}

bool AppController::selectMode(const QString& mode) {
    if (startup_state_ != "ready" || current_page_ != "welcome" || is_entering_ || mode.isEmpty() ||
        !isModeValid(mode)) {
        return false;
    }
    if (selected_mode_ == mode) {
        return true;
    }
    if (!savePreferences(mode, motion_enabled_, full_screen_, ui_size_, vertical_sync_)) {
        return false;
    }
    selected_mode_ = mode;
    emit preferencesChanged();
    return true;
}

bool AppController::enterArena() {
    if (startup_state_ != "ready" || current_page_ != "welcome" || selected_mode_.isEmpty() ||
        is_entering_) {
        return false;
    }
    is_entering_ = true;
    const auto generation = ++navigation_generation_;
    emit stateChanged();
    // 下一轮 GUI 事件切换本地容器，不用人为等待伪造加载；context 在析构时取消回调。
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != navigation_generation_) {
            return;
        }
        current_page_ = "arena";
        is_entering_ = false;
        emit stateChanged();
    });
    return true;
}

void AppController::returnToWelcome() {
    ++navigation_generation_;
    current_page_ = "welcome";
    is_entering_ = false;
    emit stateChanged();
}

bool AppController::setMotionEnabled(bool enabled) {
    if (startup_state_ != "ready" ||
        !savePreferences(selected_mode_, enabled, full_screen_, ui_size_, vertical_sync_)) {
        return false;
    }
    motion_enabled_ = enabled;
    emit preferencesChanged();
    return true;
}

bool AppController::setFullScreen(bool enabled) {
    if (startup_state_ != "ready") {
        return false;
    }
    if (full_screen_ == enabled) {
        return true;
    }
    if (!savePreferences(selected_mode_, motion_enabled_, enabled, ui_size_, vertical_sync_)) {
        return false;
    }
    full_screen_ = enabled;
    emit fullScreenChanged();
    return true;
}

bool AppController::setVerticalSync(bool enabled) {
    if (startup_state_ != "ready") return false;
    if (vertical_sync_ == enabled) return true;
    if (!savePreferences(selected_mode_, motion_enabled_, full_screen_, ui_size_, enabled))
        return false;
    vertical_sync_ = enabled;
    emit verticalSyncChanged();
    return true;
}

bool AppController::setUiSize(const QString& size) {
    if (startup_state_ != "ready" || !isUiSizeValid(size) ||
        !savePreferences(selected_mode_, motion_enabled_, full_screen_, size, vertical_sync_)) {
        return false;
    }
    ui_size_ = size;
    emit preferencesChanged();
    return true;
}

void AppController::resetPreferences() {
    // 新对象避免沿用之前 QSettings 的 FormatError；clear 可修复非法 INI 和未知键。
    QSettings settings(settings_path_, QSettings::IniFormat);
    settings.clear();
    settings.sync();
    if (settings.status() != QSettings::NoError ||
        !savePreferences({}, false, false, "medium", true)) {
        setError("无法恢复默认偏好，请检查配置目录写入权限。");
        return;
    }
    initialize();
}
