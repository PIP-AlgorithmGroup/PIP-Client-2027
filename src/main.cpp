#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "app/app_controller.h"
#include "app/match_session.h"
#include "app/ui_fonts.h"
#include "core/data_pipeline.h"
#include "video/video_receiver.h"
#include "video/video_view.h"

// NO_PLUGIN 静态模块显式注册；同一生成类型描述也提供给 qmllint/IDE。
void qml_register_types_PIPVideo();

/** 注入导航、会话和网络入口；启动不连接，官方 Topic 由会话表管理，身份须显式选择。 */
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName("PIP Client");
    QGuiApplication::setDesktopFileName(PIP_DESKTOP_ID);
    QIcon app_icon;
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512})
        app_icon.addFile(QString(":/resources/app/pip_client_%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(app_icon);
    QCoreApplication::setOrganizationName("PIP");
    QCoreApplication::setApplicationName("PIPClient");
    QCoreApplication::setApplicationVersion(PIP_VERSION);
    QQuickStyle::setStyle("Basic");
    if (!initializeUiFonts()) {
        qCritical("Bundled UI font could not be loaded; verify application resources.");
        return -1;
    }

    // 声明顺序保证 QML 先释放、数据入口再停止工作线程、最后销毁应用事件环境。
    DataPipeline pipeline;
    AppController controller;
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

    return app.exec();
}
