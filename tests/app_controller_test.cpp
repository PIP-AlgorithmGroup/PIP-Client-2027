#include "app/app_controller.h"

#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QSurfaceFormat>
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
            QVERIFY(controller.setUiSize("large"));
        }
        AppController restored(path);
        restored.initialize();
        QCOMPARE(restored.selectedMode(), QString("RMUC"));
        QVERIFY(restored.motionEnabled());
        QVERIFY(restored.fullScreen());
        QCOMPARE(restored.uiSize(), QString("large"));
        QCOMPARE(restored.uiScale(), 1.1);
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
        QTest::newRow("ui-size") << QString("ui_size") << QString("huge");
        QTest::newRow("vertical-sync") << QString("vertical_sync") << QString("maybe");
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

    void uiSizesValidatePersistAndDoNotNotifyWindowMode() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        AppController controller(path);
        controller.initialize();
        QCOMPARE(controller.uiSize(), QString("medium"));
        QCOMPARE(controller.uiScale(), 1.0);
        QSignalSpy window_spy(&controller, &AppController::fullScreenChanged);
        QVERIFY(!controller.setUiSize("huge"));
        for (const auto& size : {QString("small"), QString("medium"), QString("large")}) {
            QVERIFY(controller.setUiSize(size));
            QVERIFY(controller.selectMode("RMUL"));
            QVERIFY(controller.setMotionEnabled(true));
            AppController restored(path);
            restored.initialize();
            QCOMPARE(restored.uiSize(), size);
        }
        QCOMPARE(window_spy.count(), 0);
        QVERIFY(controller.setFullScreen(true));
        QCOMPARE(window_spy.count(), 1);
        QVERIFY(controller.setFullScreen(true));
        QCOMPARE(window_spy.count(), 1);
        controller.resetPreferences();
        QCOMPARE(controller.uiSize(), QString("medium"));
        QCOMPARE(window_spy.count(), 2);
    }

    void verticalSyncDefaultsOnAndOldConfigurationRemainsValid() {
        QTemporaryDir directory;
        AppController controller(directory.filePath("preferences.ini"));
        controller.initialize();
        QVERIFY(controller.verticalSync());
        QVERIFY(!controller.verticalSyncRestartRequired());
        QSettings legacy(directory.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue("version", 1);
        legacy.setValue("mode", "RMUL");
        legacy.setValue("motion", true);
        legacy.sync();
        AppController restored_legacy(legacy.fileName());
        restored_legacy.initialize();
        QCOMPARE(restored_legacy.startupState(), QString("ready"));
        QCOMPARE(restored_legacy.selectedMode(), QString("RMUL"));
        QVERIFY(restored_legacy.motionEnabled());
        QVERIFY(restored_legacy.verticalSync());
    }

    void verticalSyncPersistsWithoutChangingRunningWindow() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        AppController controller(path);
        controller.initialize();
        const auto previous_format = QSurfaceFormat::defaultFormat();
        const auto restore_format =
            qScopeGuard([&] { QSurfaceFormat::setDefaultFormat(previous_format); });
        controller.applyStartupGraphics();
        const auto startup_interval = QSurfaceFormat::defaultFormat().swapInterval();
        QSignalSpy window_spy(&controller, &AppController::fullScreenChanged);
        QSignalSpy sync_spy(&controller, &AppController::verticalSyncChanged);
        QVERIFY(controller.setVerticalSync(false));
        QVERIFY(!controller.verticalSync());
        QVERIFY(controller.verticalSyncRestartRequired());
        QCOMPARE(QSurfaceFormat::defaultFormat().swapInterval(), startup_interval);
        QCOMPARE(window_spy.count(), 0);
        QCOMPARE(sync_spy.count(), 1);
        QVERIFY(controller.setVerticalSync(false));
        QCOMPARE(sync_spy.count(), 1);
        // 其他偏好保存必须保留垂直同步值，且不得重新应用运行中窗口格式。
        QVERIFY(controller.setUiSize("large"));
        QVERIFY(controller.setMotionEnabled(true));
        QVERIFY(controller.setFullScreen(true));
        QVERIFY(controller.selectMode("RMUL"));
        AppController restored(path);
        restored.initialize();
        QVERIFY(!restored.verticalSync());
        QVERIFY(!restored.verticalSyncRestartRequired());
        QVERIFY(controller.setVerticalSync(true));
        QVERIFY(!controller.verticalSyncRestartRequired());
        QVERIFY(controller.setVerticalSync(false));
        controller.resetPreferences();
        QVERIFY(controller.verticalSync());
        QVERIFY(!controller.verticalSyncRestartRequired());
    }

    void verticalSyncRejectsUnreadyAndFailedWrites() {
        QTemporaryDir directory;
        const auto blocked_parent = directory.filePath("blocked");
        AppController controller(blocked_parent + "/preferences.ini");
        QVERIFY(!controller.setVerticalSync(false));
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("ready"));
        const auto previous_format = QSurfaceFormat::defaultFormat();
        const auto restore_format =
            qScopeGuard([&] { QSurfaceFormat::setDefaultFormat(previous_format); });
        controller.applyStartupGraphics();
        const auto initial_format = QSurfaceFormat::defaultFormat();
        const bool initial_request = controller.verticalSyncRequested();
        // 初始化成功后才占住父目录路径，保证后续进入保存失败分支；两平台均无需改权限。
        QFile obstruction(blocked_parent);
        QVERIFY(obstruction.open(QIODevice::WriteOnly));
        obstruction.close();
        QSignalSpy sync_spy(&controller, &AppController::verticalSyncChanged);
        QVERIFY(!controller.setVerticalSync(false));
        QCOMPARE(controller.startupState(), QString("ready"));
        QVERIFY(controller.verticalSync());
        QCOMPARE(controller.verticalSyncRequested(), initial_request);
        QVERIFY(!controller.verticalSyncRestartRequired());
        QCOMPARE(QSurfaceFormat::defaultFormat(), initial_format);
        QCOMPARE(sync_spy.count(), 0);
        QVERIFY(!controller.lastError().isEmpty());
    }

    void startupReadsSavedOffAndResetRequiresRestart() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("version", 1);
            settings.setValue("vertical_sync", false);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        const auto previous_format = QSurfaceFormat::defaultFormat();
        const auto restore_format =
            qScopeGuard([&] { QSurfaceFormat::setDefaultFormat(previous_format); });
        AppController controller(path);
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("ready"));
        QVERIFY(!controller.verticalSync());
        controller.applyStartupGraphics();
        QVERIFY(!controller.verticalSyncRequested());
        QCOMPARE(QSurfaceFormat::defaultFormat().swapInterval(), 0);
        QVERIFY(!controller.verticalSyncRestartRequired());
        controller.resetPreferences();
        QCOMPARE(controller.startupState(), QString("ready"));
        QVERIFY(controller.verticalSync());
        QVERIFY(controller.verticalSyncRestartRequired());
        QVERIFY(!controller.verticalSyncRequested());
        QCOMPARE(QSurfaceFormat::defaultFormat().swapInterval(), 0);
        // 再读配置及再次请求应用都不能把已启动的关闭状态改写成默认开启。
        controller.initialize();
        controller.applyStartupGraphics();
        QVERIFY(controller.verticalSyncRestartRequired());
        QVERIFY(!controller.verticalSyncRequested());
        QCOMPARE(QSurfaceFormat::defaultFormat().swapInterval(), 0);
        AppController restored(path);
        restored.initialize();
        QVERIFY(restored.verticalSync());
    }

    void repeatedInitializePreservesFrozenStartupRequest() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        const auto previous_format = QSurfaceFormat::defaultFormat();
        const auto restore_format =
            qScopeGuard([&] { QSurfaceFormat::setDefaultFormat(previous_format); });
        AppController controller(path);
        controller.initialize();
        controller.applyStartupGraphics();
        const auto initial_format = QSurfaceFormat::defaultFormat();
        const bool initial_request = controller.verticalSyncRequested();
        for (const bool saved : {false, true, false}) {
            {
                // 模拟欢迎页重试读取外部更新，而不是借用 setter 的内存值。
                QSettings settings(path, QSettings::IniFormat);
                settings.setValue("vertical_sync", saved);
                settings.sync();
                QCOMPARE(settings.status(), QSettings::NoError);
            }
            controller.initialize();
            QCOMPARE(controller.startupState(), QString("ready"));
            QCOMPARE(controller.verticalSync(), saved);
            QCOMPARE(controller.verticalSyncRestartRequired(), !saved);
            QCOMPARE(controller.verticalSyncRequested(), initial_request);
            controller.applyStartupGraphics();
            QCOMPARE(QSurfaceFormat::defaultFormat(), initial_format);
        }
    }

    void repairedCorruptConfigurationCannotReplaceStartupRequest() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("mode", "invalid");
            settings.setValue("vertical_sync", false);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        const auto previous_format = QSurfaceFormat::defaultFormat();
        const auto restore_format =
            qScopeGuard([&] { QSurfaceFormat::setDefaultFormat(previous_format); });
        AppController controller(path);
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("error"));
        QVERIFY(controller.verticalSync());
        controller.applyStartupGraphics();
        const auto initial_format = QSurfaceFormat::defaultFormat();
        const bool initial_request = controller.verticalSyncRequested();
        {
            // 整份配置修复后可以读到关闭偏好，但本次启动的安全默认快照必须保持冻结。
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("mode", "RMUL");
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        controller.initialize();
        QCOMPARE(controller.startupState(), QString("ready"));
        QCOMPARE(controller.selectedMode(), QString("RMUL"));
        QVERIFY(!controller.verticalSync());
        QVERIFY(controller.verticalSyncRestartRequired());
        QCOMPARE(controller.verticalSyncRequested(), initial_request);
        QVERIFY(controller.lastError().isEmpty());
        controller.applyStartupGraphics();
        QCOMPARE(QSurfaceFormat::defaultFormat(), initial_format);
    }

    void startupGraphicsUsesSavedRequestAndEnvironment_data() {
        QTest::addColumn<bool>("saved");
        QTest::addColumn<QByteArray>("override_value");
        QTest::addColumn<int>("interval");
        QTest::newRow("on") << true << QByteArray() << 1;
        QTest::newRow("off") << false << QByteArray() << 0;
        QTest::newRow("forced-off") << true << QByteArray("1") << 0;
        QTest::newRow("zero-still-forces-off") << true << QByteArray("0") << 0;
        QTest::newRow("presence-forces-off") << true << QByteArray("invalid") << 0;
    }

    void startupGraphicsUsesSavedRequestAndEnvironment() {
        QFETCH(bool, saved);
        QFETCH(QByteArray, override_value);
        QFETCH(int, interval);
        // 还原进程环境和全局默认格式，避免数据行或其他测试互相污染。
        const auto previous_environment = qgetenv("QSG_NO_VSYNC");
        const auto previous_format = QSurfaceFormat::defaultFormat();
        const auto restore = qScopeGuard([&] {
            if (previous_environment.isNull())
                qunsetenv("QSG_NO_VSYNC");
            else
                qputenv("QSG_NO_VSYNC", previous_environment);
            QSurfaceFormat::setDefaultFormat(previous_format);
        });
        if (override_value.isNull())
            qunsetenv("QSG_NO_VSYNC");
        else
            qputenv("QSG_NO_VSYNC", override_value);
        QTemporaryDir directory;
        AppController controller(directory.filePath("preferences.ini"));
        controller.initialize();
        QVERIFY(controller.setVerticalSync(saved));
        controller.applyStartupGraphics();
        QCOMPARE(QSurfaceFormat::defaultFormat().swapInterval(), interval);
        QCOMPARE(controller.verticalSyncRequested(), interval != 0);
        QVERIFY(!controller.verticalSyncRestartRequired());
        QVERIFY(controller.verticalSyncStatus().contains(interval == 0 ? "关闭" : "开启"));
        QVERIFY(controller.setVerticalSync(!saved));
        QVERIFY(controller.verticalSyncRestartRequired());
        controller.applyStartupGraphics();
        QCOMPARE(QSurfaceFormat::defaultFormat().swapInterval(), interval);
    }
};

QTEST_GUILESS_MAIN(AppControllerTest)
#include "app_controller_test.moc"
