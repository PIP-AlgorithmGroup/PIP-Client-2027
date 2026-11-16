#include "input/input_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPlatformSurfaceEvent>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

InputController::InputController(DataPipeline& pipeline, const QString& path, QObject* parent)
    : QObject(parent),
      pipeline_(pipeline),
      path_(path.isEmpty()
                ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                      .filePath("input.json")
                : path) {
    QFile file(path_);
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly) && file.size() <= 4096) {
            const auto object = QJsonDocument::fromJson(file.readAll()).object();
            const auto sensitivity = object.value("sensitivity").toDouble(-1);
            const auto frequency = object.value("frequency").toInt(-1);
            if (std::isfinite(sensitivity) && sensitivity >= .05 && sensitivity <= 10 &&
                (frequency == 75 || frequency == 200) && object.value("invertY").isBool()) {
                sensitivity_ = sensitivity;
                frequency_ = frequency;
                invert_y_ = object.value("invertY").toBool();
            } else
                config_error_ = "键鼠配置无效，已使用默认值";
        } else
            config_error_ = "无法读取键鼠配置，已使用默认值";
    }
    pipeline_.setKeyboardMouseSource(state_);
    connect(&pipeline_, &DataPipeline::connectionStateChanged, this,
            &InputController::updateIntent);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &InputController::refresh);
    timer_.start();
    updateIntent();
    refresh();
}
InputController::~InputController() {
    stopNative();
    pipeline_.setKeyboardMouseSource({});
}
QVariantMap InputController::configuration() const {
    return {{"sensitivity", sensitivity_}, {"invertY", invert_y_}, {"frequency", frequency_}};
}
QVariantMap InputController::diagnostics() const { return diagnostics_; }
bool InputController::captured() const { return captured_; }
bool InputController::testing() const { return test_page_; }
void InputController::stopNative() {
    state_->interrupt();
    native_.reset();
    if (window_) window_->unsetCursor();
}
void InputController::bindWindow(QWindow* window) {
    if (window_ == window && native_) return;
    stopNative();
    if (window_) window_->removeEventFilter(this);
    window_ = window;
    if (window_) {
        window_->installEventFilter(this);
        native_ = std::make_unique<NativeInput>(*window_, state_);
    }
    updateIntent();
}
void InputController::setContext(bool main_view, bool blocked, bool test_page) {
    if (main_view_ == main_view && blocked_ == blocked && test_page_ == test_page) return;
    main_view_ = main_view;
    blocked_ = blocked;
    test_page_ = test_page;
    if (!test_page_) test_locked_ = false;
    updateIntent();
}
void InputController::updateIntent() {
    const bool focus = window_ && window_->isActive() && window_->isVisible() &&
                       window_->visibility() != QWindow::Minimized;
    const auto mode = !focus       ? InputState::Mode::Off
                      : test_page_ ? InputState::Mode::Test
                      : main_view_ && !blocked_ && pipeline_.connectionState() == "ready"
                          ? InputState::Mode::Control
                          : InputState::Mode::Off;
    const bool lock =
        mode == InputState::Mode::Control || (mode == InputState::Mode::Test && test_locked_);
    state_->request(mode, lock, sensitivity_, invert_y_, frequency_);
    if (window_) {
        if (lock && native_)
            window_->setCursor(Qt::BlankCursor);
        else
            window_->unsetCursor();
    }
}
bool InputController::configure(double sensitivity, bool invert_y, int frequency) {
    if (!std::isfinite(sensitivity) || sensitivity < .05 || sensitivity > 10 ||
        (frequency != 75 && frequency != 200)) {
        config_error_ = "灵敏度需为 0.05–10，发送频率需为 75 或 200 Hz";
        refresh();
        return false;
    }
    const QJsonObject object{
        {"sensitivity", sensitivity}, {"invertY", invert_y}, {"frequency", frequency}};
    const auto encoded = QJsonDocument(object).toJson();
    QSaveFile file(path_);
    if (!QDir().mkpath(QFileInfo(path_).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        file.write(encoded) != encoded.size() || !file.commit()) {
        config_error_ = "键鼠配置保存失败：" + file.errorString();
        refresh();
        return false;
    }
    sensitivity_ = sensitivity;
    invert_y_ = invert_y;
    frequency_ = frequency;
    config_error_.clear();
    updateIntent();
    emit configurationChanged();
    refresh();
    return true;
}
void InputController::setTestLocked(bool locked) {
    test_locked_ = test_page_ && locked;
    updateIntent();
    refresh();
}
void InputController::resetStatistics() {
    state_->resetStatistics();
    refresh();
}
void InputController::refresh() {
    const auto sample = state_->sample(InputState::nowUs());
    if (test_locked_ && state_->requested().first == InputState::Mode::Off) {
        test_locked_ = false;
        updateIntent();
    }
    captured_ = sample.valid && sample.locked;
    if (window_) {
        if (captured_)
            window_->setCursor(Qt::BlankCursor);
        else
            window_->unsetCursor();
    }
    diagnostics_ = state_->diagnostics(InputState::nowUs());
    if (!sample.valid) {
        diagnostics_["pressedKeys"] = QStringList{};
        diagnostics_["pressedMouse"] = QStringList{};
        diagnostics_["keys"] = QVariantList{};
    }
    diagnostics_["configError"] = config_error_;
    diagnostics_["captured"] = captured_;
    diagnostics_["testLocked"] = test_locked_;
    diagnostics_["keyboardBits"] = sample.keyboard;
    diagnostics_["buttons"] = sample.buttons;
    diagnostics_["status"] = test_page_  ? (captured_ ? "本地锁定测试" : "本地键鼠测试")
                             : captured_ ? "正在控制"
                             : main_view_ && !blocked_ && pipeline_.connectionState() == "ready"
                                 ? "等待焦点 / 原生捕获"
                                 : "控制已释放";
    emit diagnosticsChanged();
}
bool InputController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_) {
        const auto [mode, wants_lock] = state_->requested();
        if (mode != InputState::Mode::Off && wants_lock) {
            // 原生线程仍接收全部输入；Qt 普通事件不得在隐藏光标下点击菜单或触发快捷键。
            // 使用锁定意图而非 33ms 诊断快照，覆盖申请锁定到确认以及撤销到 GUI 刷新的窗口。
            switch (event->type()) {
                case QEvent::MouseButtonPress:
                case QEvent::MouseButtonRelease:
                case QEvent::MouseButtonDblClick:
                case QEvent::MouseMove:
                case QEvent::Wheel:
                    event->accept();
                    return true;
                case QEvent::KeyPress:
                case QEvent::KeyRelease:
                case QEvent::ShortcutOverride:
                    // Esc 必须继续走 Qt 的菜单/Popup 路径，原生线程会独立撤销控制。
                    if (static_cast<QKeyEvent*>(event)->key() != Qt::Key_Escape) {
                        // ShortcutOverride 必须显式 accept，否则 Qt 仍可能匹配应用级快捷键。
                        event->accept();
                        return true;
                    }
                    break;
                default:
                    break;
            }
        }
        if (event->type() == QEvent::PlatformSurface) {
            const auto* surface = static_cast<QPlatformSurfaceEvent*>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                stopNative();
            else if (!native_) {
                native_ = std::make_unique<NativeInput>(*window_, state_);
                updateIntent();
            }
        } else if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) {
            state_->interrupt();
            test_locked_ = false;
            window_->unsetCursor();
        } else if (event->type() == QEvent::WindowActivate || event->type() == QEvent::Show)
            updateIntent();
    }
    return QObject::eventFilter(watched, event);
}
