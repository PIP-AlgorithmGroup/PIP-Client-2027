#include "input/input_controller.h"

#include <QTemporaryDir>
#include <QTest>
#include <limits>

/** 配置边界和重载只使用临时目录，不捕获真实窗口或连接机器人。 */
class InputControllerTest : public QObject {
    Q_OBJECT
   private slots:
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
