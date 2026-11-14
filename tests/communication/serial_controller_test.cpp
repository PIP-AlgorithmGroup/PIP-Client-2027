#include "communication/serial_controller.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSerialPort>
#include <QTemporaryDir>
#include <QTest>
#include <chrono>
#include <thread>

#include "client_to_server.pb.h"
#include "communication/data_pipeline.h"
#include "support/mqtt_test_broker.h"
#include "support/serial_test_packets.h"

/** 配置/故障在双平台运行，Linux 额外通过真实 PTY 验证串口到 MQTT 的完整链路。 */
class SerialControllerTest : public QObject {
    Q_OBJECT

   private slots:
    void defaultsPersistenceAndInvalidSettingsPreserveFile() {
        QTemporaryDir directory;
        DataPipeline pipeline;
        const auto path = directory.filePath("serial.json");
        SerialController controller(pipeline, path);
        QCOMPARE(controller.configuration().value("baudRate").toInt(), 921600);
        QCOMPARE(controller.configuration().value("dataBits").toInt(), 8);
        QCOMPARE(controller.configuration().value("parity").toString(), QString("none"));
        QCOMPARE(controller.configuration().value("stopBits").toString(), QString("1"));
        QCOMPARE(controller.configuration().value("flowControl").toString(), QString("none"));
        QCOMPARE(controller.state(), QString("disconnected"));
        auto configuration = controller.configuration();
        configuration["portName"] = "test_serial_device";
        QVERIFY(controller.configure(configuration));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = file.readAll();
        file.close();
        auto invalid = configuration;
        invalid["baudRate"] = 1.5;
        QVERIFY(!controller.configure(invalid));
        QCOMPARE(controller.configuration(), configuration);
        // 参数失败和外部文件损坏都不能把原配置覆盖为半有效结果。
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), saved);
        file.close();
        SerialController restored(pipeline, path);
        QCOMPARE(restored.configuration(), configuration);
        QCOMPARE(restored.state(), QString("disconnected"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{externally_broken");
        file.close();
        QVERIFY(!controller.configure(configuration));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{externally_broken"));
    }

    void corruptAndUnwritableConfigurationAreNotOverwritten() {
        QTemporaryDir directory;
        DataPipeline pipeline;
        const auto path = directory.filePath("corrupt.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");
        file.close();
        SerialController corrupt(pipeline, path);
        auto config = corrupt.configuration();
        config["portName"] = "test_device";
        QVERIFY(!corrupt.configure(config));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{broken"));
        file.close();
        const auto blocker_path = directory.filePath("blocked_parent");
        QFile blocker(blocker_path);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        SerialController unwritable(pipeline, blocker_path + "/serial.json");
        QVERIFY(!unwritable.configure(config));
        QVERIFY(unwritable.error().contains("原子保存"));
        QCOMPARE(unwritable.state(), QString("disconnected"));
    }

    void missingPortFailsWithoutStoppingMqttAndShutdownIsBounded() {
        QTemporaryDir directory;
        DataPipeline pipeline;
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "serial_failure_test";
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        SerialController controller(pipeline, directory.filePath("serial.json"));
        auto config = controller.configuration();
        config["portName"] = "pip_nonexistent_serial_port";
        QVERIFY(controller.configure(config));
        QVERIFY(!controller.start());
        controller.setArena(true);
        QVERIFY(controller.start());
        QTRY_COMPARE(controller.state(), QString("error"));
        QVERIFY(!controller.error().isEmpty());
        QSerialPort probe;
        probe.setPortName(config.value("portName").toString());
        QVERIFY(!probe.open(QIODevice::ReadOnly));
        // 清理失败连接不能把驱动的原始原因改成“设备尚未打开”。
        QCOMPARE(controller.error(),
                 config.value("portName").toString() + "：" + probe.errorString());
        QCOMPARE(pipeline.connectionState(), QString("ready"));
        QElapsedTimer elapsed;
        elapsed.start();
        controller.stop();
        QVERIFY(elapsed.elapsed() < 1000);
        QCOMPARE(controller.state(), QString("disconnected"));
    }

    void realSerialForwardsOnlyFreshPayloadAndReleasesPort() {
#ifndef Q_OS_LINUX
        QSKIP("完整串口 I/O 由 Linux PTY 验证；Windows 物理 USB/COM 仍需实机验收");
#else
        QTemporaryDir directory;
        serial_test::PseudoTerminal terminal;
        QVERIFY(!terminal.path().isEmpty());
        DataPipeline pipeline;
        SerialController controller(pipeline, directory.filePath("serial.json"));
        auto config = controller.configuration();
        config["portName"] = terminal.path();
        QVERIFY(controller.configure(config));
        controller.setArena(true);
        QVERIFY(controller.start());
        QTRY_COMPARE(controller.state(), QString("waiting"));
        QSerialPort occupied;
        occupied.setPortName(terminal.path());
        QVERIFY(!occupied.open(QIODevice::ReadOnly));  // 同一端口不能被第二个 Qt 串口占用。
        const auto payload = QByteArray::fromHex("00a5ff");
        const auto initial = serial_test::frame(payload, 0);
        QCOMPARE(terminal.send(initial), initial.size());
        QTest::qWait(60);  // 离线期间收到的完整控制不能在连接 MQTT 后补发。
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "serial_pty_test";
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QTest::qWait(50);
        QVERIFY(broker.received_publications.isEmpty());
        const auto fresh = serial_test::frame(payload, 1);
        QCOMPARE(terminal.send(fresh.first(5)), 5);
        QTest::qWait(5);
        QCOMPARE(terminal.send(fresh.sliced(5)), fresh.size() - 5);
        QTRY_COMPARE(broker.received_publications.size(), 1);
        QCOMPARE(broker.received_publications[0].first, QString("CustomControl"));
        rm_client_to_server::CustomControl message;
        QVERIFY(message.ParseFromString(broker.received_publications[0].second.toStdString()));
        QCOMPARE(QByteArray::fromStdString(message.data()), payload);
        QVERIFY(message.has_data());
        QTest::qWait(150);
        QCOMPARE(broker.received_publications.size(), 1);  // 不周期重发最后一帧。
        QTRY_COMPARE(controller.state(), QString("waiting"));

        const auto stale = serial_test::frame("stale", 2);
        QCOMPARE(terminal.send(stale), stale.size());
        // 阻塞 GUI、保持产品串口线程运行，验证邮箱旧帧不会在 GUI 恢复后发出。
        QThread::msleep(170);
        QTest::qWait(60);
        QCOMPARE(broker.received_publications.size(), 1);

        QByteArray burst;
        for (int index = 0; index < 100; ++index)
            burst += serial_test::frame(QByteArray::number(index), quint8(index + 3));
        QCOMPARE(terminal.send(burst), burst.size());
        QTRY_VERIFY(broker.received_publications.size() > 1);
        QTest::qWait(100);
        QVERIFY(broker.received_publications.size() <= 4);
        QVERIFY(message.ParseFromString(broker.received_publications.last().second.toStdString()));
        QCOMPARE(QByteArray::fromStdString(message.data()), QByteArray("99"));

        QTimer producer;
        producer.setInterval(1);
        producer.setTimerType(Qt::PreciseTimer);
        int next_seq = 103;
        QByteArray latest_payload;
        bool is_write_complete = true;
        connect(&producer, &QTimer::timeout, this, [&] {
            latest_payload = "stream_" + QByteArray::number(next_seq);
            const auto packet = serial_test::frame(latest_payload, quint8(next_seq++));
            if (terminal.send(packet) != packet.size()) is_write_complete = false;
        });
        const auto before_stream = broker.received_publications.size();
        QElapsedTimer stream_clock;
        stream_clock.start();
        producer.start();
        QTest::qWait(1000);
        producer.stop();
        const auto duration_us = stream_clock.nsecsElapsed() / 1000;
        QTest::qWait(150);  // 允许最后一份最新值完成本地转发，不产生追赶队列。
        QVERIFY(is_write_complete);
        const auto publications = broker.received_publications.size() - before_stream;
        QVERIFY(publications > 0);
        QVERIFY(publications <= duration_us / 13334 + 2);
        QCOMPARE(controller.diagnostics().value("failedCount").toULongLong(), quint64(0));
        QVERIFY(message.ParseFromString(broker.received_publications.last().second.toStdString()));
        QCOMPARE(QByteArray::fromStdString(message.data()), latest_payload);

        pipeline.stop();
        const auto offline = serial_test::frame("offline", 103);
        QCOMPARE(terminal.send(offline), offline.size());
        QTest::qWait(50);
        const auto sent_before = broker.received_publications.size();
        const auto offline_partial = serial_test::frame("old_partial", 104);
        QCOMPARE(terminal.send(offline_partial.first(8)), 8);
        QTest::qWait(20);
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        QCOMPARE(terminal.send(offline_partial.sliced(8)), offline_partial.size() - 8);
        QTest::qWait(80);
        QCOMPARE(broker.received_publications.size(), sent_before);
        terminal.closeMaster();
        QTRY_COMPARE(controller.state(), QString("error"));
        QCOMPARE(pipeline.connectionState(), QString("ready"));
        controller.setArena(false);
        QCOMPARE(controller.state(), QString("disconnected"));

        serial_test::PseudoTerminal reconnected;
        config["portName"] = reconnected.path();
        QVERIFY(controller.configure(config));
        controller.setArena(true);
        QVERIFY(controller.start());
        QTRY_COMPARE(controller.state(), QString("waiting"));
        controller.setArena(false);
        QSerialPort probe;
        probe.setPortName(reconnected.path());
        QVERIFY(probe.open(QIODevice::ReadOnly));
#endif
    }

    void realSerialSustainsFiftyHertzWithoutRateRejection_data() {
        QTest::addColumn<int>("gui_busy_ms");
        QTest::addColumn<int>("gui_period_ms");
        QTest::newRow("idle_gui") << 0 << 16;
        QTest::newRow("gui_busy_8ms_per_16ms") << 8 << 16;
        QTest::newRow("gui_busy_12ms_per_25ms") << 12 << 25;
        QTest::newRow("gui_busy_14ms_per_31ms") << 14 << 31;
    }

    void realSerialSustainsFiftyHertzWithoutRateRejection() {
#ifndef Q_OS_LINUX
        QSKIP("本用例使用Linux PTY；不抢占用户正在使用的Windows实体COM");
#else
        QFETCH(int, gui_busy_ms);
        QFETCH(int, gui_period_ms);
        QTemporaryDir directory;
        serial_test::PseudoTerminal terminal;
        TestBroker broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost));
        DataPipeline pipeline;
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.serverPort();
        settings.client_id = "serial_cadence_test";
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        SerialController controller(pipeline, directory.filePath("serial.json"));
        auto config = controller.configuration();
        config["portName"] = terminal.path();
        QVERIFY(controller.configure(config));
        controller.setArena(true);
        QVERIFY(controller.start());
        QTRY_COMPARE(controller.state(), QString("waiting"));
        QEventLoop cadence_loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &cadence_loop, &QEventLoop::quit);
        QTimer gui_work;
        gui_work.setTimerType(Qt::PreciseTimer);
        gui_work.setInterval(gui_period_ms);
        connect(&gui_work, &QTimer::timeout, this, [gui_busy_ms] { QThread::msleep(gui_busy_ms); });
        int produced = 0;
        bool is_complete = true;
        // 独立源不依赖GUI计时；否则GUI忙时源也变慢，会掩盖实际50Hz输入被合并的问题。
        // 线程只写PTY master；join前不读取其计数，且terminal始终比线程长寿。
        std::jthread source([&] {
            const auto start = std::chrono::steady_clock::now();
            for (; produced < 50; ++produced) {
                std::this_thread::sleep_until(start +
                                              std::chrono::milliseconds(20 * (produced + 1)));
                const auto packet =
                    serial_test::frame(QByteArray(60, char(produced)), quint8(produced));
                if (terminal.send(packet) != packet.size()) is_complete = false;
            }
        });
        // 使用产品同款连续事件循环；QTRY/qWait的分段等待会人为延迟14ms冷却和完成反馈，
        // 将测试框架自身的唤醒粒度误判为产品吞吐。独立deadline保证失败也能有界退出。
        deadline.start(1500);
        if (gui_busy_ms) gui_work.start();
        cadence_loop.exec();
        gui_work.stop();
        source.join();
        QCOMPARE(produced, 50);
        QCOMPARE(broker.received_publications.size(), 50);
        QVERIFY(is_complete);
        QCOMPARE(controller.diagnostics().value("failedCount").toULongLong(), quint64(0));
        for (int i = 0; i < 50; ++i) {
            rm_client_to_server::CustomControl message;
            QVERIFY(message.ParseFromString(broker.received_publications[i].second.toStdString()));
            QCOMPARE(QByteArray::fromStdString(message.data()), QByteArray(60, char(i)));
        }
        controller.stop();
        pipeline.stop();
#endif
    }
};

QTEST_GUILESS_MAIN(SerialControllerTest)
#include "serial_controller_test.moc"
