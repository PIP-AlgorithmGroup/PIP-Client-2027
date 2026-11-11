#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

#include "app/app_controller.h"
#include "core/data_pipeline.h"

/** 启动欢迎页并注入本地导航与通用数据入口；不选择 topic、字段或机器人身份。 */
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("PIP");
    QCoreApplication::setApplicationName("PIPClient");
    QCoreApplication::setApplicationVersion(PIP_VERSION);
    QQuickStyle::setStyle("Basic");

    // 声明顺序保证 QML 先释放、数据入口再停止工作线程、最后销毁应用事件环境。
    DataPipeline pipeline;
    AppController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{"dataPipeline", QVariant::fromValue(static_cast<QObject*>(&pipeline))},
         {"appController", QVariant::fromValue(static_cast<QObject*>(&controller))}});
    engine.loadFromModule("PIPClient", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCritical("Welcome window could not be created; see QML diagnostics above.");
        return -1;
    }

    // 窗口先创建，下一轮事件读取真实本地配置，不用假进度或网络检查阻塞首帧。
    QTimer::singleShot(0, &controller, &AppController::initialize);

    return app.exec();
}
