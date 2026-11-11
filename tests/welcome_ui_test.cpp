#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <memory>
#include <utility>

#include "app/app_controller.h"
#include "core/data_pipeline.h"

/** 实际 QML 窗口交互回归，临时偏好与 offscreen 平台隔离用户状态。 */
class WelcomeUiTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() { QQuickStyle::setStyle("Basic"); }

    void init() {
        directory_ = std::make_unique<QTemporaryDir>();
        controller_ = std::make_unique<AppController>(directory_->filePath("preferences.ini"));
        pipeline_ = std::make_unique<DataPipeline>();
        engine_ = std::make_unique<QQmlEngine>();
        connect(engine_.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& errors) {
            for (const auto& error : errors) {
                warnings_.append(error.toString());
            }
        });
        QQmlComponent component(engine_.get(), QUrl::fromLocalFile(PIP_MAIN_QML_PATH));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        window_.reset(qobject_cast<QQuickWindow*>(component.createWithInitialProperties(
            {{"dataPipeline", QVariant::fromValue(static_cast<QObject*>(pipeline_.get()))},
             {"appController", QVariant::fromValue(static_cast<QObject*>(controller_.get()))}})));
        QVERIFY2(window_, qPrintable(component.errorString()));
        controller_->initialize();
        window_->requestActivate();
        QTRY_COMPARE(window_->property("backgroundState").toString(), QString("ready"));
    }

    void cleanup() {
        // 先释放界面，避免依赖对象析构时触发仍在求值的 QML 绑定。
        window_.reset();
        engine_.reset();
        pipeline_.reset();
        controller_.reset();
        directory_.reset();
        const auto warnings = std::exchange(warnings_, {});
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void firstLaunchIsOfflineAndRequiresMode() {
        auto* enter = item("enterArenaButton");
        QVERIFY(enter);
        QVERIFY(!enter->isEnabled());
        QCOMPARE(pipeline_->connectionState(), QString("offline"));
        QVERIFY(controller_->selectedMode().isEmpty());
    }

    void mouseNavigationAndReturn() {
        click("championshipMode");
        QCOMPARE(controller_->selectedMode(), QString("RMUC"));
        click("enterArenaButton");
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        QVERIFY(item("arenaPage")->isVisible());
        QVERIFY(!item("welcomePage")->isVisible());
        click("backToWelcomeButton");
        QCOMPARE(controller_->currentPage(), QString("welcome"));
        QCOMPARE(pipeline_->connectionState(), QString("offline"));
    }

    void keyboardSelectionAndActivation() {
        item("leagueMode")->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Right);
        QCOMPARE(controller_->selectedMode(), QString("RMUC"));
        QTest::keyClick(window_.get(), Qt::Key_Left);
        QCOMPARE(controller_->selectedMode(), QString("RMUL"));
        item("enterArenaButton")->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Return);
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
    }

    void settingsRemainBoundAndKeyboardShortcutsWork() {
        click("tool设置");
        QTRY_VERIFY(window_->findChild<QObject*>("toolsPanel")->property("opened").toBool());
        click("motionSwitch");
        QVERIFY(controller_->motionEnabled());
        QVERIFY(controller_->setMotionEnabled(false));
        QVERIFY(!item("motionSwitch")->property("checked").toBool());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTest::keyClick(window_.get(), Qt::Key_F11);
        QTRY_VERIFY(controller_->fullScreen());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(!controller_->fullScreen());
        item("leagueMode")->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Space);
        QCOMPARE(controller_->selectedMode(), QString("RMUL"));
        QTest::keyClick(window_.get(), Qt::Key_Tab);
        QVERIFY(item("championshipMode")->hasActiveFocus());
    }

    void settingsWriteFailureRestoresSwitch() {
        // 在初始化后用目录占据偏好路径，稳定复现保存失败而不依赖权限模型。
        QVERIFY(QDir().mkdir(directory_->filePath("preferences.ini")));
        click("tool设置");
        QTRY_VERIFY(window_->findChild<QObject*>("toolsPanel")->property("opened").toBool());
        click("motionSwitch");
        QVERIFY(!controller_->motionEnabled());
        QVERIFY(!item("motionSwitch")->property("checked").toBool());
        QVERIFY(!controller_->lastError().isEmpty());
    }

    void captureWelcomeWhenRequested() {
        // 按需输出实际 QML 渲染截图；普通回归不在源码树写入图像。
        const auto capture_dir = qEnvironmentVariable("PIP_UI_CAPTURE_DIR");
        if (capture_dir.isEmpty()) {
            return;
        }
        QVERIFY(controller_->selectMode("RMUL"));
        QTest::qWait(200);
        const auto screenshot = window_->grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(QDir(capture_dir).filePath("welcome_runtime.png")));
        click("tool设置");
        QTRY_VERIFY(window_->findChild<QObject*>("toolsPanel")->property("opened").toBool());
        QVERIFY(window_->grabWindow().save(QDir(capture_dir).filePath("settings_runtime.png")));
    }

    void panelsCloseAndRestoreFocus_data() {
        QTest::addColumn<QString>("panel");
        for (const auto* name : {"设置", "诊断", "帮助", "关于"}) {
            QTest::newRow(name) << QString::fromUtf8(name);
        }
    }

    void panelsCloseAndRestoreFocus() {
        QFETCH(QString, panel);
        click(("tool" + panel).toUtf8().constData());
        auto* popup = window_->findChild<QObject*>("toolsPanel");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(popup->property("section").toString(), panel);
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("opened").toBool());
        QVERIFY(item("leagueMode")->hasActiveFocus());
    }

    void layoutFitsMinimumWindow() {
        window_->resize(1024, 640);
        QCoreApplication::processEvents();
        for (const auto* name :
             {"leagueMode", "championshipMode", "enterArenaButton", "tool设置", "tool关于"}) {
            auto* target = item(name);
            QVERIFY(target);
            const auto position = target->mapToScene(QPointF());
            QVERIFY(position.x() >= 0 && position.y() >= 0);
            QVERIFY(position.x() + target->width() <= window_->width());
            QVERIFY(position.y() + target->height() <= window_->height());
            QVERIFY(target->height() >= 44);
        }
    }

    void brokenBackgroundPreservesNavigation_data() {
        QTest::addColumn<bool>("break_poster");
        QTest::newRow("poster-fallback") << false;
        QTest::newRow("solid-fallback") << true;
    }

    void brokenBackgroundPreservesNavigation() {
        QFETCH(bool, break_poster);
        // 只允许预期的图片缺失告警，其他 QML 绑定/布局错误仍由 cleanup 拦截。
        window_->setProperty("backgroundSource", QUrl("qrc:/missing-background.jpg"));
        if (break_poster) {
            window_->setProperty("posterSource", QUrl("qrc:/missing-poster.jpg"));
        }
        QTRY_COMPARE(window_->property("backgroundState").toString(), QString("error"));
        // 两张异步图都失败后再清理精确匹配的预期告警。
        if (break_poster) {
            QTRY_VERIFY(std::any_of(
                warnings_.cbegin(), warnings_.cend(),
                [](const QString& warning) { return warning.contains("missing-poster"); }));
        }
        warnings_.removeIf([](const QString& warning) {
            return warning.contains("missing-background") || warning.contains("missing-poster");
        });
        click("leagueMode");
        click("enterArenaButton");
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
    }

    void motionStopsOutsideWelcome() {
        QVERIFY(controller_->setMotionEnabled(true));
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        QVERIFY(!window_->property("backgroundMotionRunning").toBool());
        controller_->returnToWelcome();
        window_->hide();
        QVERIFY(!window_->property("backgroundMotionRunning").toBool());
    }

   private:
    QQuickItem* item(const char* name) const {
        // Repeater delegate 的 QObject 所有者不一定是可视父项；按实际场景树查找。
        const auto find = [&](auto&& self, QQuickItem* parent) -> QQuickItem* {
            if (parent->objectName() == QString::fromUtf8(name)) {
                return parent;
            }
            for (auto* child : parent->childItems()) {
                if (auto* match = self(self, child)) {
                    return match;
                }
            }
            return nullptr;
        };
        return find(find, window_->contentItem());
    }

    void click(const char* name) {
        auto* target = item(name);
        QVERIFY2(target, name);
        // Popup opened 先于延迟布局；从祖先到控件完成 polish 后再计算点击坐标。
        QList<QQuickItem*> ancestors;
        for (auto* current = target; current; current = current->parentItem()) {
            ancestors.prepend(current);
        }
        for (auto* ancestor : ancestors) {
            ancestor->ensurePolished();
        }
        const auto point = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        QTest::mouseClick(window_.get(), Qt::LeftButton, Qt::NoModifier, point.toPoint());
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::unique_ptr<AppController> controller_;
    std::unique_ptr<DataPipeline> pipeline_;
    std::unique_ptr<QQmlEngine> engine_;
    std::unique_ptr<QQuickWindow> window_;
    QStringList warnings_;
};

QTEST_MAIN(WelcomeUiTest)
#include "welcome_ui_test.moc"
