#include <QDir>
#include <QFontMetrics>
#include <QGuiApplication>
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
#include "app/ui_fonts.h"
#include "core/data_pipeline.h"

/** 实际 QML 窗口交互回归，临时偏好与 offscreen 平台隔离用户状态。 */
class WelcomeUiTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        QVERIFY(initializeUiFonts());
        QCoreApplication::setApplicationVersion(PIP_VERSION);
    }

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

    void toolsShowIconsWithAccessibleNames() {
        for (const auto* name : {"设置", "诊断", "帮助", "关于"}) {
            auto* button = item((QString("tool") + name).toUtf8().constData());
            QVERIFY(button);
            const auto source = button->property("iconSource").toUrl();
            QVERIFY(source.toString().startsWith("qrc:/resources/icons/"));
            QCOMPARE(button->property("text").toString(), QString::fromUtf8(name));
            QVERIFY(qFuzzyCompare(button->width(), button->height()));
            auto* icon = button->findChild<QQuickItem*>("buttonIcon");
            QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(), 1);
            QVERIFY(icon->isVisible());
        }
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

    void modeSelectionPreservesMaximizedWindow() {
        window_->resize(1440, 860);
        window_->showMaximized();
        QTRY_COMPARE(window_->visibility(), QWindow::Maximized);
        const auto maximized_size = window_->size();
        click("championshipMode");
        QCOMPARE(window_->visibility(), QWindow::Maximized);
        QCOMPARE(window_->size(), maximized_size);
        QVERIFY(controller_->setUiSize("large"));
        QVERIFY(controller_->setMotionEnabled(true));
        QCOMPARE(window_->visibility(), QWindow::Maximized);
        click("enterArenaButton");
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        click("backToWelcomeButton");
        QCOMPARE(window_->visibility(), QWindow::Maximized);
        QCOMPARE(window_->size(), maximized_size);
    }

    void modeNavigationPreservesResizedWindow() {
        const QSize resized(1440, 860);
        window_->resize(resized);
        click("championshipMode");
        QCOMPARE(window_->size(), resized);
        click("leagueMode");
        click("enterArenaButton");
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        QCOMPARE(window_->size(), resized);
        click("backToWelcomeButton");
        QCOMPARE(window_->size(), resized);
    }

    void fullscreenRestoresPreviousWindowState_data() {
        QTest::addColumn<bool>("maximize");
        QTest::newRow("normal") << false;
        QTest::newRow("maximized") << true;
    }

    void fullscreenRestoresPreviousWindowState() {
        QFETCH(bool, maximize);
        window_->resize(1440, 860);
        if (maximize) {
            window_->showMaximized();
        }
        QCoreApplication::processEvents();
        const auto original_state = window_->visibility();
        const auto original_size = window_->size();
        QVERIFY(controller_->setFullScreen(true));
        QTRY_COMPARE(window_->visibility(), QWindow::FullScreen);
        QVERIFY(controller_->selectMode("RMUC"));
        QCOMPARE(window_->visibility(), QWindow::FullScreen);
        QVERIFY(controller_->setFullScreen(false));
        QTRY_COMPARE(window_->visibility(), original_state);
        QTRY_COMPARE(window_->size(), original_size);
    }

    void resizingPreservesUiSize_data() {
        QTest::addColumn<QString>("ui_size");
        for (const auto* size : {"small", "medium", "large"}) {
            QTest::newRow(size) << QString(size);
        }
    }

    void resizingPreservesUiSize() {
        QFETCH(QString, ui_size);
        QVERIFY(controller_->setUiSize(ui_size));
        // 档位变化会排队更新 Column；等待底部控件布局完成后再记录窗口缩放基线。
        QTRY_COMPARE(
            window_->height() - item("enterArenaButton")
                                    ->mapToScene(QPointF(0, item("enterArenaButton")->height()))
                                    .y(),
            64 * controller_->uiScale());
        auto* title = item("titleText");
        const auto title_size = title->property("font").value<QFont>().pixelSize();
        const auto button_size = item("enterArenaButton")->size();
        const auto bottom_gap =
            window_->height() -
            item("enterArenaButton")->mapToScene(QPointF(0, button_size.height())).y();
        auto* tools = item("welcomeTools");
        const auto tools_bottom_gap =
            window_->height() - tools->mapToScene(QPointF(0, tools->height())).y();
        const auto tools_right_gap =
            window_->width() - tools->mapToScene(QPointF(tools->width(), 0)).x();
        const auto original_tool_position = item("tool设置")->mapToScene(QPointF());
        window_->resize(1600, 960);
        QCoreApplication::processEvents();
        QCOMPARE(title->property("font").value<QFont>().pixelSize(), title_size);
        QCOMPARE(item("enterArenaButton")->size(), button_size);
        QCOMPARE(window_->height() -
                     item("enterArenaButton")->mapToScene(QPointF(0, button_size.height())).y(),
                 bottom_gap);
        QCOMPARE(window_->height() - tools->mapToScene(QPointF(0, tools->height())).y(),
                 tools_bottom_gap);
        QCOMPARE(window_->width() - tools->mapToScene(QPointF(tools->width(), 0)).x(),
                 tools_right_gap);
        QVERIFY(item("tool设置")->mapToScene(QPointF()) != original_tool_position);
    }

    void chineseUsesPackagedSansFont() {
        QCOMPARE(QGuiApplication::font().family(), QString("Noto Sans SC"));
        auto* subtitle = item("welcomeSubtitle");
        const auto font = subtitle->property("font").value<QFont>();
        QCOMPARE(font.family(), QString("Noto Sans SC"));
        const QFontMetrics metrics(font);
        for (const auto character : QString("机甲大师高校联盟赛超级对抗赛设置诊断帮助关于")) {
            QVERIFY(metrics.inFont(character));
        }
    }

    void uiSizeSettingAppliesWithoutResizingWindow() {
        window_->resize(1440, 860);
        const auto window_size = window_->size();
        const auto medium_size = item("enterArenaButton")->size();
        click("tool设置");
        QTRY_VERIFY(window_->findChild<QObject*>("toolsPanel")->property("opened").toBool());
        click("uiSize_large");
        QCOMPARE(controller_->uiSize(), QString("large"));
        QCOMPARE(window_->size(), window_size);
        QVERIFY(item("enterArenaButton")->width() > medium_size.width());
        click("uiSize_small");
        QCOMPARE(controller_->uiSize(), QString("small"));
        QVERIFY(item("enterArenaButton")->width() < medium_size.width());
        QVERIFY(item("uiSize_small")->property("checked").toBool());
        QVERIFY(!item("uiSize_large")->property("checked").toBool());
    }

    void uiSizeWriteFailurePreservesAppliedSelection() {
        QVERIFY(QDir().mkdir(directory_->filePath("preferences.ini")));
        click("tool设置");
        QTRY_VERIFY(window_->findChild<QObject*>("toolsPanel")->property("opened").toBool());
        click("uiSize_large");
        QCOMPARE(controller_->uiSize(), QString("medium"));
        QVERIFY(!item("uiSize_large")->property("checked").toBool());
        QVERIFY(item("uiSize_medium")->property("checked").toBool());
        QVERIFY(!controller_->lastError().isEmpty());
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

    void layoutFitsMinimumWindow_data() {
        QTest::addColumn<QString>("ui_size");
        QTest::addColumn<QSize>("window_size");
        for (const auto* size : {"small", "medium", "large"}) {
            for (const auto resolution : {QSize(1024, 640), QSize(1280, 720), QSize(1920, 1080)}) {
                const auto name =
                    QString("%1-%2x%3").arg(size).arg(resolution.width()).arg(resolution.height());
                QTest::newRow(qPrintable(name)) << QString(size) << resolution;
            }
        }
    }

    void layoutFitsMinimumWindow() {
        QFETCH(QString, ui_size);
        QFETCH(QSize, window_size);
        QVERIFY(controller_->setUiSize(ui_size));
        window_->resize(window_size);
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
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            const auto filename = QString("welcome_%1_%2x%3.png")
                                      .arg(ui_size)
                                      .arg(window_size.width())
                                      .arg(window_size.height());
            QVERIFY(window_->grabWindow().save(
                QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR")).filePath(filename)));
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
