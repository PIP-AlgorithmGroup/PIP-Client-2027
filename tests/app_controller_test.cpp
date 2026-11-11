#include "app/app_controller.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

/** 本地导航和偏好回归，不依赖用户配置、网络、图形驱动或比赛字段。 */
class AppControllerTest : public QObject {
    Q_OBJECT

   private slots:
    void firstLaunchRequiresSelection() {
        QTemporaryDir directory;
        AppController controller(directory.filePath("preferences.ini"));
        QCOMPARE(controller.startupState(), QString("loading"));
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("ready"));
        QVERIFY(controller.selectedMode().isEmpty());
        QVERIFY(!controller.enterArena());
        QCOMPARE(controller.currentPage(), QString("welcome"));
    }

    void bothModesNavigateWithoutDuplicateEntry_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("league") << QString("RMUL");
        QTest::newRow("championship") << QString("RMUC");
    }

    void bothModesNavigateWithoutDuplicateEntry() {
        QFETCH(QString, mode);
        QTemporaryDir directory;
        AppController controller(directory.filePath("preferences.ini"));
        controller.initialize();
        QVERIFY(!controller.selectMode("invalid"));
        QVERIFY(controller.selectMode(mode));
        QVERIFY(controller.enterArena());
        QVERIFY(!controller.enterArena());
        QVERIFY(controller.isEntering());
        QTRY_COMPARE(controller.currentPage(), QString("arena"));
        QVERIFY(!controller.selectMode(mode == "RMUL" ? "RMUC" : "RMUL"));
        controller.returnToWelcome();
        QCOMPARE(controller.currentPage(), QString("welcome"));
        QCOMPARE(controller.selectedMode(), mode);
    }

    void preferencesRestoreWithoutAutomaticEntry() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        {
            AppController controller(path);
            controller.initialize();
            QVERIFY(controller.selectMode("RMUC"));
            QVERIFY(controller.setMotionEnabled(true));
            QVERIFY(controller.setFullScreen(true));
        }
        AppController restored(path);
        restored.initialize();
        QCOMPARE(restored.selectedMode(), QString("RMUC"));
        QVERIFY(restored.motionEnabled());
        QVERIFY(restored.fullScreen());
        QCOMPARE(restored.currentPage(), QString("welcome"));
        QVERIFY(!restored.isEntering());
    }

    void corruptPreferencesApplyNoPartialValues() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue("mode", "unknown");
        settings.setValue("motion", true);
        settings.sync();
        AppController controller(path);
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("error"));
        QVERIFY(controller.selectedMode().isEmpty());
        QVERIFY(!controller.motionEnabled());
        QVERIFY(!controller.lastError().isEmpty());
        controller.resetPreferences();
        QCOMPARE(controller.startupState(), QString("ready"));
        QVERIFY(controller.lastError().isEmpty());
        AppController restored(path);
        restored.initialize();
        QCOMPARE(restored.startupState(), QString("ready"));
    }

    void invalidBooleanAndVersionAreRejected_data() {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::newRow("boolean") << QString("motion") << QString("maybe");
        QTest::newRow("future-version") << QString("version") << QString("99");
    }

    void invalidBooleanAndVersionAreRejected() {
        QFETCH(QString, key);
        QFETCH(QString, value);
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(key, value);
        settings.sync();
        AppController controller(path);
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("error"));
    }

    void writeFailurePreservesAppliedState() {
        QTemporaryDir directory;
        // 目录不能作为 INI 文件；跨平台模拟写入失败，不依赖 chmod 或管理员权限。
        AppController controller(directory.path());
        controller.initialize();
        QVERIFY(!controller.selectMode("RMUL"));
        QVERIFY(controller.selectedMode().isEmpty());
        QVERIFY(!controller.lastError().isEmpty());
    }

    void cancelledEntryCannotNavigateLater() {
        QTemporaryDir directory;
        AppController controller(directory.filePath("preferences.ini"));
        controller.initialize();
        QVERIFY(controller.selectMode("RMUL"));
        QVERIFY(controller.enterArena());
        controller.returnToWelcome();
        QCoreApplication::processEvents();
        QCOMPARE(controller.currentPage(), QString("welcome"));
        QVERIFY(!controller.isEntering());
    }
};

QTEST_GUILESS_MAIN(AppControllerTest)
#include "app_controller_test.moc"
