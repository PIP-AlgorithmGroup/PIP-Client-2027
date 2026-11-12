#include <QDir>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <memory>
#include <utility>

#include "app/app_controller.h"
#include "app/match_session.h"
#include "app/ui_fonts.h"
#include "core/data_pipeline.h"
#include "video/video_receiver.h"
#include "video/video_view.h"
#include "video_test_packets.h"

// 测试直接加载源码 QML，使用与产品一致的静态类型注册。
void qml_register_types_PIPVideo();

/** 实际 QML 窗口交互回归，临时偏好与 offscreen 平台隔离用户状态。 */
class WelcomeUiTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qml_register_types_PIPVideo();
        QVERIFY(initializeUiFonts());
        QCoreApplication::setApplicationVersion(PIP_VERSION);
    }

    void init() {
        directory_ = std::make_unique<QTemporaryDir>();
        controller_ = std::make_unique<AppController>(directory_->filePath("preferences.ini"));
        pipeline_ = std::make_unique<DataPipeline>();
        video_ = std::make_unique<VideoReceiver>(directory_->filePath("video.ini"));
        session_ = std::make_unique<MatchSession>(*pipeline_, *video_,
                                                  directory_->filePath("session.json"));
        connect(controller_.get(), &AppController::stateChanged, session_.get(), [this] {
            session_->setArena(controller_->currentPage() == "arena", controller_->selectedMode());
        });
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
             {"appController", QVariant::fromValue(static_cast<QObject*>(controller_.get()))},
             {"videoReceiver", QVariant::fromValue(static_cast<QObject*>(video_.get()))},
             {"matchSession", QVariant::fromValue(static_cast<QObject*>(session_.get()))}})));
        QVERIFY2(window_, qPrintable(component.errorString()));
        controller_->initialize();
        window_->requestActivate();
        QTRY_COMPARE(window_->property("backgroundState").toString(), QString("ready"));
    }

    void cleanup() {
        // 先释放界面，避免依赖对象析构时触发仍在求值的 QML 绑定。
        window_.reset();
        engine_.reset();
        session_.reset();
        pipeline_.reset();
        video_.reset();
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

    void sessionFormEditsOneConfigurationAndRestoresDraftOnly() {
        for (const auto& entry : session_->interfaces()) {
            const auto interface = entry.toMap();
            if (interface.value("address").toString() == "127.0.0.1") {
                QVERIFY(session_->configure(interface.value("name").toString(), "127.0.0.1", 103,
                                            "127.0.0.1", 1883));
                break;
            }
        }
        QCOMPARE(session_->configuration().value("brokerPort").toInt(), 1883);
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        click("leagueMode");
        click("enterArenaButton");
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* menu = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(!item("sessionProfile_official"));
        QVERIFY(!item("sessionProfile_development"));
        QCOMPARE(item("sessionBrokerHost")->property("text").toString(), QString("127.0.0.1"));
        QCOMPARE(item("sessionBrokerPort")->property("text").toString(), QString("1883"));
        for (const auto* name : {"sessionBrokerHost", "sessionBrokerPort", "sessionLocalAddress"}) {
            QVERIFY(!item(name)->property("readOnly").toBool());
        }
        item("sessionBrokerHost")->setProperty("text", "192.0.2.10");
        item("sessionBrokerPort")->setProperty("text", "1884");
        item("sessionLocalAddress")->setProperty("text", "127.0.0.2");
        click("sessionRestoreDefaults");
        QCOMPARE(item("sessionBrokerHost")->property("text").toString(), QString("192.168.12.1"));
        QCOMPARE(item("sessionBrokerPort")->property("text").toString(), QString("3333"));
        QCOMPARE(item("sessionLocalAddress")->property("text").toString(), QString("192.168.12.2"));
        QCOMPARE(item("sessionTeam")->property("currentValue").toString(), QString("blue"));
        QCOMPARE(item("sessionRobotType")->property("currentValue").toInt(), 103);
        QCOMPARE(session_->configuration().value("brokerPort").toInt(), 1883);
        QVERIFY(video_->running());
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            window_->update();
            QTest::qWait(80);
            QVERIFY(window_->grabWindow().save(
                QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR")).filePath("session_unified.png")));
        }
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QCOMPARE(pipeline_->connectionState(), QString("offline"));
        QVERIFY(!session_->active());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(item("sessionBrokerHost")->property("text").toString(), QString("127.0.0.1"));
        QCOMPARE(item("sessionBrokerPort")->property("text").toString(), QString("1883"));
        QCOMPARE(item("sessionTeam")->property("currentValue").toString(), QString("blue"));
        QCOMPARE(item("sessionRobotType")->property("currentValue").toInt(), 103);
        QVERIFY(video_->running());
    }

    void rmucCanOpenSessionSettingsWithoutStartingVideo() {
        click("championshipMode");
        click("enterArenaButton");
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* menu = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(item("sessionBrokerHost")->isVisible());
        QVERIFY(!item("menuSection_video")->isEnabled());
        QVERIFY(!video_->running());
        QCOMPARE(pipeline_->connectionState(), QString("offline"));
        click("videoReturnButton");
        QCOMPARE(controller_->currentPage(), QString("welcome"));
        QVERIFY(!menu->property("opened").toBool());
    }

    void robotIdentityRequiresTeamThenTypeAndKeepsDraftLocal() {
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        click("leagueMode");
        click("enterArenaButton");
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* menu = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        auto* team = item("sessionTeam");
        auto* robot = item("sessionRobotType");
        QCOMPARE(team->property("currentIndex").toInt(), -1);
        QVERIFY(!robot->isEnabled());
        QVERIFY(!item("sessionConnect")->isEnabled());
        // 使用实际键盘激活下拉项；阵营变化必须清除兵种，不能只更新显示的阵营名。
        team->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Down);
        QCOMPARE(team->property("currentValue").toString(), QString("red"));
        QVERIFY(robot->isEnabled());
        QCOMPARE(robot->property("currentIndex").toInt(), -1);
        robot->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Down);
        QCOMPARE(robot->property("currentValue").toInt(), 1);
        QTest::keyClick(window_.get(), Qt::Key_Down);
        QCOMPARE(robot->property("currentValue").toInt(), 3);
        team->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Down);
        QCOMPARE(team->property("currentValue").toString(), QString("blue"));
        QCOMPARE(robot->property("currentIndex").toInt(), -1);
        robot->forceActiveFocus();
        QTest::keyClick(window_.get(), Qt::Key_Down);
        QCOMPARE(robot->property("currentValue").toInt(), 101);
        QTest::keyClick(window_.get(), Qt::Key_Down);
        QCOMPARE(robot->property("currentValue").toInt(), 103);
        QCOMPARE(session_->configuration().value("robotId").toInt(), 0);
        QVERIFY(item("sessionRobotId")->property("text").toString().contains("103"));
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(team->property("currentIndex").toInt(), -1);
        QVERIFY(!robot->isEnabled());
        QCOMPARE(pipeline_->connectionState(), QString("offline"));
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
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(window_->findChild<QObject*>("clientMenu")->property("opened").toBool());
        click("videoReturnButton");
        QCOMPARE(controller_->currentPage(), QString("welcome"));
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

    void rmulShowsOnlyAspectFittedVideoAndEscapeMenu() {
        const auto port = video_test::freePort();
        QVERIFY(video_->configure("127.0.0.1", port, 0));
        window_->resize(1280, 800);
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        QTRY_COMPARE(video_->status(), QString("等待图传"));
        QVERIFY(!item("backToWelcomeButton")->isVisible());
        auto* viewport = qobject_cast<VideoView*>(item("rmulVideo"));
        QVERIFY(viewport);
        QVERIFY(viewport->isVisible());
        QVERIFY(viewport->contentRect().isEmpty());
        QTest::qWait(30);
        auto blank = window_->grabWindow();
        QVERIFY(!blank.isNull());
        QVERIFY(item("videoPlaceholder"));
        QVERIFY(item("videoPlaceholder")->isVisible());
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            QVERIFY(blank.save(
                QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR")).filePath("rmul_waiting.png")));
        }
        QCOMPARE(item("videoEmptyTitle")->property("text").toString(), QString("等待图传画面"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1, 0, true));
        QTRY_VERIFY(!viewport->contentRect().isEmpty());
        QVERIFY(!item("videoPlaceholder")->isVisible());
        QCOMPARE(viewport->contentRect(), QRectF(0, 40, 1280, 720));
        QTest::qWait(40);
        auto picture = window_->grabWindow();
        QCOMPARE(picture.pixelColor(picture.width() / 2, 0), QColor(Qt::black));
        QVERIFY(picture.pixelColor(picture.width() / 2, picture.height() / 2) != QColor(Qt::black));
        // 连续上传验证纹理替换与旧纹理释放，避免单张截图掩盖生命周期错误。
        QSignalSpy frame_spy(video_.get(), &VideoReceiver::frameChanged);
        QVERIFY(video_test::sendFrame(sender, port, 2));
        QTRY_VERIFY(!frame_spy.isEmpty());
        QTest::qWait(40);
        QVERIFY(!window_->grabWindow().isNull());
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            QVERIFY(picture.save(
                QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR")).filePath("rmul_video.png")));
        }
        window_->resize(1440, 800);
        QTRY_COMPARE(viewport->contentRect(),
                     QRectF((1440.0 - 800.0 * 16 / 9) / 2, 0, 800.0 * 16 / 9, 800));
        QVERIFY(controller_->setFullScreen(true));
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* settings = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(settings->property("opened").toBool());
        QVERIFY(controller_->fullScreen());
        QCOMPARE(settings->property("section").toString(), QString("session"));
        QVERIFY(!item("videoSettings")->isVisible());
        click("menuSection_video");
        QTRY_VERIFY(item("videoSettings")->isVisible());
        QCOMPARE(item("videoBindAddress")->property("text").toString(), QString("127.0.0.1"));
        QTRY_COMPARE(item("videoSourceEndpoint")->property("text").toString(),
                     QString("当前发送端  127.0.0.1 : ") + QString::number(sender.localPort()));
        click("videoStopButton");
        QVERIFY(!video_->running());
        QCOMPARE(item("videoSourceEndpoint")->property("text").toString(),
                 QString("当前发送端  尚未发现"));
        QVERIFY(viewport->contentRect().isEmpty());
        QCOMPARE(item("videoEmptyTitle")->property("text").toString(), QString("图传已暂停"));
        click("videoApplyButton");
        QTRY_VERIFY(video_->running());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(!settings->property("opened").toBool());
        QVERIFY(controller_->fullScreen());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(settings->property("opened").toBool());
        click("videoReturnButton");
        QCOMPARE(controller_->currentPage(), QString("welcome"));
        QVERIFY(!video_->running());
    }

    void videoSettingsFitsUiSizesAndPreservesDraft_data() {
        QTest::addColumn<QString>("ui_size");
        for (const auto* size : {"small", "medium", "large"}) {
            QTest::newRow(size) << QString(size);
        }
    }

    void videoEmptyStatesFollowActualReception() {
        const auto port = video_test::freePort();
        QVERIFY(video_->configure("127.0.0.1", port, 0));
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(video_->status(), QString("等待图传"));
        auto* overlay = item("businessOverlay");
        QVERIFY(overlay);
        // 模拟未来展示项：按窗口坐标持有最后值，视频状态不得重建或清空它。
        auto* readout = new QQuickItem(overlay);
        readout->setProperty("lastValue", 42);
        readout->setPosition(QPointF(24, 24));
        const auto verify_overlay = [&] {
            QCOMPARE(item("businessOverlay"), overlay);
            QVERIFY(overlay->isVisible());
            QCOMPARE(overlay->size(), QSizeF(window_->size()));
            QCOMPARE(readout->parentItem(), overlay);
            QCOMPARE(readout->property("lastValue").toInt(), 42);
            QCOMPARE(readout->position(), QPointF(24, 24));
        };
        verify_overlay();
        QVERIFY(item("videoPlaceholder")->isVisible());
        QCOMPARE(item("videoEmptyTitle")->property("text").toString(), QString("等待图传画面"));
        QUdpSocket sender;
        QVERIFY(video_test::sendFrame(sender, port, 1));
        QTRY_VERIFY(!item("videoPlaceholder")->isVisible());
        verify_overlay();
        QTRY_VERIFY_WITH_TIMEOUT(item("videoPlaceholder")->isVisible(), 2500);
        verify_overlay();
        QCOMPARE(item("videoEmptyTitle")->property("text").toString(), QString("图传信号中断"));
        video_->stop();
        verify_overlay();
        QCOMPARE(item("videoEmptyTitle")->property("text").toString(), QString("图传已暂停"));
        QUdpSocket occupied;
        QVERIFY(occupied.bind(QHostAddress::LocalHost, port, QUdpSocket::DontShareAddress));
        video_->start();
        QTRY_COMPARE(video_->status(), QString("接收失败"));
        verify_overlay();
        QCOMPARE(item("videoEmptyTitle")->property("text").toString(), QString("图传暂不可用"));
        click("videoEmptySettingsButton");
        QTRY_VERIFY(window_->findChild<QObject*>("clientMenu")->property("opened").toBool());
        QVERIFY(!item("videoSettings")->isVisible());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QVERIFY(!window_->findChild<QObject*>("clientMenu")->property("opened").toBool());
        occupied.close();
        video_->start();
        QTRY_COMPARE(video_->status(), QString("等待图传"));
        QVERIFY(video_test::sendFrame(sender, port, 2, 0, true));
        QTRY_VERIFY(!item("videoPlaceholder")->isVisible());
        window_->resize(1440, 900);
        QTRY_COMPARE(overlay->size(), QSizeF(1440, 900));
        verify_overlay();
    }

    void sessionActionsStayOutsideScrollingForm_data() {
        QTest::addColumn<QString>("ui_size");
        for (const auto* size : {"small", "medium", "large"}) {
            QTest::newRow(size) << QString(size);
        }
    }

    void sessionActionsStayOutsideScrollingForm() {
        QFETCH(QString, ui_size);
        QVERIFY(controller_->setUiSize(ui_size));
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        window_->resize(1024, 640);
        click("leagueMode");
        click("enterArenaButton");
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(window_->findChild<QObject*>("clientMenu")->property("opened").toBool());
        auto* form = item("sessionFormScroll");
        auto* connect_button = item("sessionConnect");
        QVERIFY(form);
        QVERIFY(connect_button);
        QTRY_VERIFY(connect_button->mapToScene(QPointF()).y() >=
                    form->mapToScene(QPointF(0, form->height())).y());
        const auto position = connect_button->mapToScene(QPointF());
        auto* flickable = form->property("contentItem").value<QObject*>();
        QVERIFY(flickable);
        flickable->setProperty("contentY", flickable->property("contentHeight").toReal() -
                                               flickable->property("height").toReal());
        QCOMPARE(connect_button->mapToScene(QPointF()), position);
        for (const auto* name : {"sessionConnect", "sessionDisconnect", "menuResumeButton"}) {
            auto* target = item(name);
            const auto bottom = target->mapToScene(QPointF(target->width(), target->height()));
            QVERIFY(bottom.x() <= window_->width());
            QVERIFY(bottom.y() <= window_->height());
        }
    }

    void videoSettingsFitsUiSizesAndPreservesDraft() {
        QFETCH(QString, ui_size);
        QVERIFY(controller_->setUiSize(ui_size));
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        window_->resize(1024, 640);
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* settings = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(settings->property("opened").toBool());
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            QTest::qWait(50);
            QVERIFY(window_->grabWindow().save(
                QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR")).filePath("rmul_menu.png")));
        }
        click("menuSection_video");
        QVERIFY(item("videoPortLabel")->property("text").toString().contains("默认 3334"));
        QCOMPARE(item("videoSpec_format")->property("text").toString(),
                 QString("1920 × 1080 · 60 帧/秒"));
        QCOMPARE(item("videoSpec_header")->property("text").toString(), QString("8 字节 · 大端"));
        QCOMPARE(item("videoSpec_retry")->property("text").toString(), QString("不支持"));
        for (const auto* name :
             {"videoCloseButton", "videoReturnButton", "videoStopButton", "videoApplyButton"}) {
            auto* target = item(name);
            QVERIFY(target);
            target->ensurePolished();
            const auto top_left = target->mapToScene(QPointF());
            QVERIFY(top_left.x() >= 0 && top_left.y() >= 0);
            QVERIFY(top_left.x() + target->width() <= window_->width());
            QVERIFY(top_left.y() + target->height() <= window_->height());
            QVERIFY(target->height() >= 36);
        }
        auto* form = item("videoFormScroll");
        QVERIFY(form);
        form->ensurePolished();
        auto* port_field = item("videoPort");
        // Popup opened 早于布局完成，等待实际几何，避免采到上一档的旧布局。
        QTRY_VERIFY(port_field->mapToScene(QPointF(0, port_field->height())).y() <=
                    form->mapToScene(QPointF(0, form->height())).y());
        item("videoBindAddress")->setProperty("text", "192.0.2.1");
        item("videoPort")->setProperty("text", "");
        QVERIFY(!item("videoApplyButton")->isEnabled());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QCOMPARE(video_->bindAddress(), QString("127.0.0.1"));
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(settings->property("opened").toBool());
        QCOMPARE(settings->property("section").toString(), QString("session"));
        click("menuSection_video");
        QCOMPARE(item("videoBindAddress")->property("text").toString(), QString("127.0.0.1"));
        click("videoIndex_1");
        QCOMPARE(item("videoIndexBase")->property("currentIndex").toInt(), 1);
        QCOMPARE(video_->indexBase(), 0);
        const auto changed_port = video_test::freePort();
        QVERIFY(changed_port != 0);
        item("videoPort")->setProperty("text", QString::number(changed_port));
        click("videoApplyButton");
        QCOMPARE(video_->indexBase(), 1);
        QCOMPARE(video_->port(), int(changed_port));
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            QTest::qWait(50);
            QVERIFY(
                window_->grabWindow().save(QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR"))
                                               .filePath("video_settings_" + ui_size + ".png")));
        }
        // 官方规格在滚动区域下半部；最小窗口大档也必须可达，不能被固定动作栏挡住。
        auto* flickable = form->property("contentItem").value<QObject*>();
        QVERIFY(flickable);
        flickable->setProperty("contentY", flickable->property("contentHeight").toReal() -
                                               flickable->property("height").toReal());
        auto* last_spec = item("videoSpec_retry");
        QTRY_VERIFY(last_spec->mapToScene(QPointF()).y() >= form->mapToScene(QPointF()).y());
        QTRY_VERIFY(last_spec->mapToScene(QPointF(0, last_spec->height())).y() <=
                    form->mapToScene(QPointF(0, form->height())).y());
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            QTest::qWait(50);
            QVERIFY(window_->grabWindow().save(QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR"))
                                                   .filePath("video_specs_" + ui_size + ".png")));
        }
    }

    void clientMenuResizesWithoutScalingControls_data() {
        QTest::addColumn<QString>("ui_size");
        for (const auto* size : {"small", "medium", "large"}) {
            QTest::newRow(size) << QString(size);
        }
    }

    void clientMenuResizesWithoutScalingControls() {
        QFETCH(QString, ui_size);
        QVERIFY(controller_->setUiSize(ui_size));
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        window_->resize(1280, 800);
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(video_->status(), QString("等待图传"));
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* menu = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        click("menuSection_video");
        auto* apply = item("videoApplyButton");
        auto* form = item("videoFormScroll");
        QTRY_VERIFY(form->height() > 0);
        const auto initial_width = menu->property("width").toReal();
        const auto initial_height = menu->property("height").toReal();
        const auto initial_form_height = form->height();
        const auto button_size = apply->size();
        const auto field_height = item("videoPort")->height();
        const auto field_font = item("videoPort")->property("font").value<QFont>();
        item("videoBindAddress")->setProperty("text", "192.0.2.1");
        window_->resize(1920, 1080);
        QTRY_VERIFY(menu->property("width").toReal() >= initial_width);
        QTRY_VERIFY(menu->property("height").toReal() >= initial_height);
        QVERIFY(menu->property("width").toReal() <= 1120 * controller_->uiScale());
        QVERIFY(menu->property("height").toReal() <= 740 * controller_->uiScale());
        QVERIFY(menu->property("width").toReal() <= window_->width() * 0.8);
        QVERIFY(menu->property("height").toReal() <= window_->height() * 0.85);
        QTRY_VERIFY(form->height() >= initial_form_height);
        QCOMPARE(apply->size(), button_size);
        QCOMPARE(item("videoPort")->height(), field_height);
        QCOMPARE(item("videoPort")->property("font").value<QFont>(), field_font);
        QCOMPARE(menu->property("section").toString(), QString("video"));
        QCOMPARE(item("videoBindAddress")->property("text").toString(), QString("192.0.2.1"));
        if (!qEnvironmentVariableIsEmpty("PIP_UI_CAPTURE_DIR")) {
            QTest::qWait(50);
            QVERIFY(window_->grabWindow().save(QDir(qEnvironmentVariable("PIP_UI_CAPTURE_DIR"))
                                                   .filePath("menu_resized_" + ui_size + ".png")));
        }
        window_->resize(1024, 640);
        QTRY_VERIFY(menu->property("width").toReal() <= 1024 - 48);
        QTRY_VERIFY(menu->property("height").toReal() <= 640 - 48);
        for (const auto* name : {"videoCloseButton", "menuResumeButton", "videoApplyButton"}) {
            auto* target = item(name);
            QTRY_VERIFY(target->mapToScene(QPointF()).x() >= 0);
            QTRY_VERIFY(target->mapToScene(QPointF()).y() >= 0);
            QTRY_VERIFY(target->mapToScene(QPointF(target->width(), target->height())).x() <=
                        window_->width());
            QTRY_VERIFY(target->mapToScene(QPointF(target->width(), target->height())).y() <=
                        window_->height());
        }
        QVERIFY(video_->running());
    }

    void clientMenuSupportsSectionsWithoutInterruptingReception() {
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(video_->status(), QString("等待图传"));
        window_->resize(1280, 800);
        const auto original_size = window_->size();
        const auto headline_size = item("videoEmptyTitle")->property("font").value<QFont>();
        window_->resize(1440, 900);
        QCOMPARE(item("videoEmptyTitle")->property("font").value<QFont>(), headline_size);
        window_->resize(original_size);
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* menu = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("section").toString(), QString("session"));
        QVERIFY(video_->running());
        click("menuSection_video");
        item("videoBindAddress")->setProperty("text", "192.0.2.1");
        click("menuSection_display");
        click("menuUiSize_large");
        QCOMPARE(controller_->uiSize(), QString("large"));
        QCOMPARE(window_->size(), original_size);
        click("menuSection_diagnostics");
        QCOMPARE(menu->property("section").toString(), QString("diagnostics"));
        click("menuSection_video");
        QCOMPARE(item("videoBindAddress")->property("text").toString(), QString("192.0.2.1"));
        QCOMPARE(video_->bindAddress(), QString("127.0.0.1"));
        click("menuResumeButton");
        QTRY_VERIFY(!menu->property("opened").toBool());
        QVERIFY(video_->running());
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("section").toString(), QString("session"));
        click("menuSection_video");
        QCOMPARE(item("videoBindAddress")->property("text").toString(), QString("127.0.0.1"));
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

    void rmulLayoutAcrossWindowsAndUiSizes_data() {
        QTest::addColumn<QSize>("window_size");
        QTest::addColumn<QString>("ui_size");
        for (const auto size : {QSize(1024, 640), QSize(1280, 720), QSize(1920, 1080)}) {
            for (const auto* ui_size : {"small", "medium", "large"}) {
                const auto name =
                    QString("%1x%2_%3").arg(size.width()).arg(size.height()).arg(ui_size);
                QTest::newRow(qPrintable(name)) << size << QString(ui_size);
            }
        }
    }

    void rmulLayoutAcrossWindowsAndUiSizes() {
        QFETCH(QSize, window_size);
        QFETCH(QString, ui_size);
        QVERIFY(controller_->setUiSize(ui_size));
        window_->resize(window_size);
        QVERIFY(video_->configure("127.0.0.1", video_test::freePort(), 0));
        click("leagueMode");
        click("enterArenaButton");
        QTRY_COMPARE(video_->status(), QString("等待图传"));
        const auto capture = [&](const QString& section) {
            const auto dir = qEnvironmentVariable("PIP_UI_CAPTURE_DIR");
            if (!dir.isEmpty()) {
                QTest::qWait(60);
                const auto filename = QString("rmul_%1_%2x%3_%4.png")
                                          .arg(section)
                                          .arg(window_size.width())
                                          .arg(window_size.height())
                                          .arg(ui_size);
                QVERIFY(window_->grabWindow().save(QDir(dir).filePath(filename)));
            }
        };
        capture("waiting");
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        auto* menu = window_->findChild<QObject*>("clientMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        for (const auto* section : {"session", "video", "display", "diagnostics"}) {
            click(qPrintable(QString("menuSection_") + section));
            QTRY_COMPARE(menu->property("section").toString(), QString(section));
            // 所有常驻动作必须留在窗口内，滚动内容不得扩张菜单宽度。
            auto* close = item("videoCloseButton");
            QTRY_VERIFY(close->mapToScene(QPointF(close->width(), close->height())).x() <=
                        window_->width());
            capture(section);
        }
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

    void motionPreviewsFromSettings() {
        click("tool设置");
        QTRY_VERIFY(window_->findChild<QObject*>("toolsPanel")->property("opened").toBool());
        click("motionSwitch");
        QVERIFY(controller_->motionEnabled());
        // 检查实际图片父项的变换，而非只验证开关或“允许播放”的状态。
        auto* picture = item("arenaBackground")->parentItem();
        QTRY_VERIFY_WITH_TIMEOUT(picture->scale() > 1.002, 2000);
        const auto preview_scale = picture->scale();
        QTest::keyClick(window_.get(), Qt::Key_Escape);
        QVERIFY(picture->scale() >= preview_scale);
        QTRY_VERIFY_WITH_TIMEOUT(picture->scale() > preview_scale + 0.001, 2000);
        QVERIFY(controller_->setMotionEnabled(false));
        QTRY_COMPARE(picture->scale(), 1.0);
    }

    void motionStopsOutsideWelcome() {
        QVERIFY(controller_->setMotionEnabled(true));
        auto* picture = item("arenaBackground")->parentItem();
        QTRY_VERIFY_WITH_TIMEOUT(picture->scale() > 1.002, 2000);
        QVERIFY(controller_->selectMode("RMUL"));
        QVERIFY(controller_->enterArena());
        QTRY_COMPARE(controller_->currentPage(), QString("arena"));
        QVERIFY(!window_->property("backgroundMotionRunning").toBool());
        QTRY_COMPARE(picture->scale(), 1.0);
        controller_->returnToWelcome();
        window_->hide();
        QVERIFY(!window_->property("backgroundMotionRunning").toBool());
        QTRY_COMPARE(picture->scale(), 1.0);
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
    std::unique_ptr<VideoReceiver> video_;
    std::unique_ptr<MatchSession> session_;
    std::unique_ptr<QQmlEngine> engine_;
    std::unique_ptr<QQuickWindow> window_;
    QStringList warnings_;
};

QTEST_MAIN(WelcomeUiTest)
#include "welcome_ui_test.moc"
