#include "input/input_controller.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

/** 配置边界和重载只使用临时目录，不捕获真实窗口或连接机器人。 */
class InputControllerTest : public QObject {
    Q_OBJECT
   private slots:
    void masterSwitchDefaultsOffWithoutRestoringPreviousOutput() {
        QTemporaryDir directory;
        DataPipeline pipeline;
        const auto path = directory.filePath("input.json");
        {
            InputController input(pipeline, path);
            QCOMPARE(input.configuration().value("enabled"), QVariant(false));
            input.setEnabled(true);
            QVERIFY(input.configure(2.5, true, 75));
            QCOMPARE(input.configuration().value("enabled"), QVariant(true));
        }
        InputController loaded(pipeline, path);
        QCOMPARE(loaded.configuration().value("enabled"), QVariant(false));
        QCOMPARE(loaded.configuration().value("sensitivity"), QVariant(2.5));
    }
    void outputCanBeDisabledWhenConfigurationCannotBeSaved() {
        QTemporaryDir directory;
        QFile obstruction(directory.filePath("file"));
        QVERIFY(obstruction.open(QIODevice::WriteOnly));
        obstruction.close();
        DataPipeline pipeline;
        InputController input(pipeline, obstruction.fileName() + "/input.json");
        input.setEnabled(true);
        QVERIFY(!input.configure(2.5, true, 75));
        input.setEnabled(false);
        QCOMPARE(input.configuration().value("enabled"), QVariant(false));
    }
    void legacyConfigurationLoadsWithMasterSwitchOff() {
        QTemporaryDir directory;
        const auto path = directory.filePath("input.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        // 旧版曾保存开启状态；加载时只保留参数，不恢复控制意图。
        file.write("{\"sensitivity\":2.5,\"invertY\":true,\"frequency\":75,\"enabled\":true}");
        file.close();
        DataPipeline pipeline;
        InputController input(pipeline, path);
        QCOMPARE(input.configuration().value("enabled"), QVariant(false));
        QCOMPARE(input.configuration().value("sensitivity"), QVariant(2.5));
    }
    void invalidConfigurationPreservesWholeSettings() {
        QTemporaryDir directory;
        DataPipeline pipeline;
        InputController input(pipeline, directory.filePath("input.json"));
        const auto initial = input.configuration();
        QVERIFY(!input.configure(std::numeric_limits<double>::quiet_NaN(), true, 75));
        QCOMPARE(input.configuration(), initial);
        QVERIFY(!input.configure(1, true, 1000));
        QCOMPARE(input.configuration(), initial);
    }
    void configurationSurvivesReconstruction() {
        QTemporaryDir directory;
        DataPipeline pipeline;
        const auto path = directory.filePath("input.json");
        {
            InputController input(pipeline, path);
            QVERIFY(input.configure(2.5, true, 75));
        }
        InputController loaded(pipeline, path);
        QCOMPARE(loaded.configuration().value("sensitivity").toDouble(), 2.5);
        QCOMPARE(loaded.configuration().value("invertY").toBool(), true);
        QCOMPARE(loaded.configuration().value("frequency").toInt(), 75);
        QVERIFY(!loaded.captured());
    }
};
QTEST_GUILESS_MAIN(InputControllerTest)
#include "input_controller_test.moc"
