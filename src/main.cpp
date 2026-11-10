#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "core/data_pipeline.h"

/** 启动空白界面并注入通用数据入口；不选择业务 topic、字段或机器人身份。 */
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    // 声明顺序保证 QML 先释放、数据入口再停止工作线程、最后销毁应用事件环境。
    DataPipeline pipeline;
    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{"dataPipeline", QVariant::fromValue(static_cast<QObject*>(&pipeline))}});
    engine.loadFromModule("PIPClient", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
