#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "communication/data_pipeline.h"
#include "communication/serial_controller.h"

/**
 * 有界手工联调入口：使用产品 SerialController/DataPipeline，不替换解析器或 MQTT。
 * 参数来自调用者；配置只写临时目录，退出先关闭串口并 join，再停止 MQTT。
 * ready 仅表示本地串口和 MQTT 均就绪，不表示远端机器人执行。
 */
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Product serial-to-MQTT hardware probe; no UI or user config");
    parser.addHelpOption();
    parser.addOption({"port", "Serial device", "device"});
    parser.addOption({"host", "MQTT IPv4 host", "host", "127.0.0.1"});
    parser.addOption({"mqtt-port", "MQTT TCP port", "port", "13333"});
    parser.addOption({"seconds", "Lifetime in seconds, 1..300", "seconds", "20"});
    parser.process(application);
    bool is_port_valid = false;
    bool is_duration_valid = false;
    const auto port = parser.value("mqtt-port").toUInt(&is_port_valid);
    const auto seconds = parser.value("seconds").toInt(&is_duration_valid);
    if (parser.value("port").isEmpty() || !is_port_valid || port == 0 || port > 65535 ||
        !is_duration_valid || seconds < 1 || seconds > 300)
        parser.showHelp(2);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    DataPipeline pipeline;
    SerialController serial(pipeline, directory.filePath("serial.json"));
    auto config = serial.configuration();
    config["portName"] = parser.value("port");
    if (!serial.configure(config)) return 2;
    MqttSettings settings;
    settings.host = parser.value("host");
    settings.port = quint16(port);
    settings.client_id = "pip_serial_probe_" + QString::number(application.applicationPid());
    bool is_ready = false;
    auto report = [&] {
        const bool ready = pipeline.connectionState() == "ready" &&
                           (serial.state() == "waiting" || serial.state() == "receiving");
        if (ready && !is_ready) {
            is_ready = true;
            QTextStream(stdout) << "ready\n" << Qt::flush;
        }
        if (serial.state() == "error" || pipeline.connectionState() == "error") {
            QTextStream(stderr) << serial.error() << " / " << pipeline.lastError() << Qt::endl;
            application.exit(1);
        }
    };
    QObject::connect(&pipeline, &DataPipeline::connectionStateChanged, &application, report);
    QObject::connect(&serial, &SerialController::statusChanged, &application, report);
    if (!pipeline.start(settings)) return 2;
    // 在 MQTT 就绪后才打开串口，使源端可按 ready 同步，避免启动时控制被正确丢弃。
    QObject::connect(&pipeline, &DataPipeline::connectionStateChanged, &application, [&] {
        if (pipeline.connectionState() == "ready" && serial.state() == "disconnected") {
            serial.setArena(true);
            serial.start();
        }
    });
    QTimer::singleShot(seconds * 1000, &application, [&] { application.exit(is_ready ? 0 : 1); });
    const auto result = application.exec();
    QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(serial.diagnostics()))
                               .toJson(QJsonDocument::Compact)
                        << Qt::endl;
    serial.stop();
    pipeline.stop();
    return result;
}
