#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>

#include "app/app_controller.h"
#include "app/match_session.h"
#include "app/ui_fonts.h"
#include "communication/data_pipeline.h"
#include "diagnostics/runtime_diagnostics.h"
#include "diagnostics/session_log.h"
#include "platform/crash_reporter.h"
#include "video/ffmpeg_log_bridge.h"
#include "video/video_receiver.h"
#include "video/video_view.h"

// NO_PLUGIN 静态模块显式注册；同一生成类型描述也提供给 qmllint/IDE。
void qml_register_types_PIPVideo();

/** 注入导航、会话和网络入口；启动不连接，官方 Topic 由会话表管理，身份须显式选择。 */
static int runClient(int argc, char* argv[], SessionLog& log) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName("PIP Client");
    QGuiApplication::setDesktopFileName(PIP_DESKTOP_ID);
    QIcon app_icon;
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512})
        app_icon.addFile(QString(":/resources/app/pip_client_%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(app_icon);
    QQuickStyle::setStyle("Basic");
    log.record("info", "application", "gui_initialized",
               {{"platform", QGuiApplication::platformName()},
                {"buildRevision", PIP_BUILD_REVISION},
                {"buildType", PIP_BUILD_TYPE}});
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&] { log.record("info", "application", "shutdown_started"); });
    if (!initializeUiFonts()) {
        qCritical("Bundled UI font could not be loaded; verify application resources.");
        return -1;
    }

    // 声明顺序保证 QML 先释放、数据入口再停止工作线程、最后销毁应用事件环境。
    DataPipeline pipeline;
    AppController controller;
    controller.setLogLocation(log.rootDirectory(), log.error());
    // 同步请求必须在 QML 创建首个窗口前确定；无效偏好保留安全默认并由界面报告错误。
    controller.initialize();
    controller.applyStartupGraphics();
    VideoReceiver video_receiver;
    MatchSession session(pipeline, video_receiver);
    // C++ 会话唯一管理页面与网络生命周期，QML 不再直接启停后台线程。
    QObject::connect(&controller, &AppController::stateChanged, &session, [&] {
        session.setArena(controller.currentPage() == "arena", controller.selectedMode());
    });
    qml_register_types_PIPVideo();
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
        for (const auto& error : errors) log.record("warning", "qml", error.toString());
    });
    engine.setInitialProperties(
        {{"dataPipeline", QVariant::fromValue(static_cast<QObject*>(&pipeline))},
         {"appController", QVariant::fromValue(static_cast<QObject*>(&controller))},
         {"videoReceiver", QVariant::fromValue(static_cast<QObject*>(&video_receiver))},
         {"matchSession", QVariant::fromValue(static_cast<QObject*>(&session))}});
    engine.loadFromModule("PIPClient", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCritical("Welcome window could not be created; see QML diagnostics above.");
        return -1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        qCritical("Root QML object is not a window.");
        return -1;
    }
    RuntimeDiagnostics diagnostics(log, controller, pipeline, video_receiver, session, *window);
    return app.exec();
}

/** 日志与原生崩溃句柄先于 GUI 初始化，晚于全部业务线程和 Qt 对象释放。 */
int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("PIP");
    QCoreApplication::setApplicationName("PIPClient");
    QCoreApplication::setApplicationVersion(PIP_VERSION);
    SessionLog log;
    if (!log.installQtHandler())
        qWarning("File logging is unavailable; application will continue.");
    CrashReporter crash;
    QString crash_error;
    if (!crash.install(log.sessionDirectory(), &crash_error))
        log.record("warning", "crash", "capture_unavailable", {{"error", crash_error}});
    FfmpegLogBridge ffmpeg_log;
    const auto result = runClient(argc, argv, log);
    log.finish(result);
    return result;
}
