#include "runtime_diagnostics.h"

#include <QJsonArray>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include "app/app_controller.h"
#include "app/match_session.h"
#include "app/session_log.h"
#include "core/data_pipeline.h"
#include "video/video_receiver.h"
#include "video/video_view.h"

RuntimeDiagnostics::RuntimeDiagnostics(SessionLog& log, AppController& controller,
                                       DataPipeline& pipeline, VideoReceiver& video,
                                       MatchSession& session, QQuickWindow& window) {
    const auto app_state = [&] {
        log.record("info", "application", "state_changed",
                   {{"startup", controller.startupState()},
                    {"page", controller.currentPage()},
                    {"mode", controller.selectedMode()},
                    {"error", controller.lastError()},
                    {"verticalSync", controller.verticalSync()},
                    {"uiSize", controller.uiSize()},
                    {"fullscreen", controller.fullScreen()},
                    {"motion", controller.motionEnabled()},
                    {"syncRequested", controller.verticalSyncRequested()},
                    {"restartRequired", controller.verticalSyncRestartRequired()}});
    };
    connect(&controller, &AppController::stateChanged, this, app_state);
    connect(&controller, &AppController::errorChanged, this, app_state);
    connect(&controller, &AppController::verticalSyncChanged, this, app_state);
    connect(&controller, &AppController::preferencesChanged, this, app_state);
    connect(&controller, &AppController::fullScreenChanged, this, app_state);
    const auto video_state = [&] {
        log.record("info", "video", "state_changed",
                   {{"running", video.running()},
                    {"status", video.status()},
                    {"error", video.error()},
                    {"bind", video.bindAddress()},
                    {"port", video.port()},
                    {"decoderChoice", video.decoderChoice()}});
    };
    connect(&video, &VideoReceiver::statusChanged, this, video_state);
    connect(&video, &VideoReceiver::configurationChanged, this, video_state);
    connect(&video, &VideoReceiver::sourceChanged, this, [&] {
        log.record("info", "video", "source_changed",
                   {{"address", video.sourceAddress()}, {"port", video.sourcePort()}});
    });
    connect(&video, &VideoReceiver::decoderDevicesChanged, this, [&] {
        if (!video.probingDevices())
            log.record("info", "video", "devices_detected",
                       {{"devices", QJsonArray::fromVariantList(video.decoderDevices())}});
    });
    const auto network_state = [&] {
        log.record("info", "mqtt", "state_changed",
                   {{"state", pipeline.connectionState()}, {"error", pipeline.lastError()}});
    };
    connect(&pipeline, &DataPipeline::connectionStateChanged, this, network_state);
    connect(&pipeline, &DataPipeline::lastErrorChanged, this, network_state);
    const auto session_state = [&] {
        log.record("info", "session", "state_changed",
                   {{"active", session.active()},
                    {"networkReady", session.networkReady()},
                    {"error", session.error()},
                    {"configuration", QJsonObject::fromVariantMap(session.configuration())}});
    };
    connect(&session, &MatchSession::statusChanged, this, session_state);
    connect(&session, &MatchSession::configurationChanged, this, session_state);
    connect(&window, &QWindow::visibilityChanged, this, [&](QWindow::Visibility visibility) {
        log.record("info", "window", "visibility_changed", {{"visibility", int(visibility)}});
    });
    connect(&window, &QQuickWindow::sceneGraphError, this,
            [&](QQuickWindow::SceneGraphError code, const QString& message) {
                log.record("error", "graphics", "scene_graph_error",
                           {{"code", int(code)}, {"message", message}});
            });
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [&] {
        controller.setLogLocation(log.rootDirectory(), log.error());
        auto* viewport = window.findChild<VideoView*>("rmulVideo");
        log.record(
            "info", "diagnostics", "runtime_snapshot",
            {{"graphics", window.property("graphicsBackend").toString()},
             {"windowWidth", window.width()},
             {"windowHeight", window.height()},
             {"screenHz", window.screen() ? window.screen()->refreshRate() : 0},
             {"video", QJsonObject::fromVariantMap(video.diagnostics())},
             {"output", viewport ? QJsonObject::fromVariantMap(viewport->presentationDiagnostics())
                                 : QJsonObject{}},
             {"mqtt", QJsonObject::fromVariantMap(pipeline.diagnostics())}});
    });
    app_state();
    video_state();
    network_state();
    session_state();
    timer->start();
}
