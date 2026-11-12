#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

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

    // 窗口先创建，下一轮事件读取真实本地配置，不用假进度或网络检查阻塞首帧。
    QTimer::singleShot(0, &controller, &AppController::initialize);

    return app.exec();
}
