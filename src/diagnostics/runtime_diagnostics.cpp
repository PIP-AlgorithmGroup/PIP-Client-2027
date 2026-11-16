#include "runtime_diagnostics.h"

#include <QJsonArray>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <chrono>

#include "app/app_controller.h"
#include "app/match_session.h"
#include "communication/data_pipeline.h"
#include "diagnostics/session_log.h"
#include "video/presentation_window.h"
#include "video/video_receiver.h"
#include "video/video_view.h"

namespace {
qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

RuntimeDiagnostics::RuntimeDiagnostics(SessionLog& log, AppController& controller,
                                       DataPipeline& pipeline, VideoReceiver& video,
                                       MatchSession& session, QQuickWindow& window) {
    const auto recovery_events = [&log, &video] {
        const auto batch = video.takeRecoveryEvents();
        const auto events = batch.value("events").toList();
        // 异常事件含最近 8 次入队及解码证据；每条最多 4 项，保留 16KiB 单条预算余量。
        for (qsizetype offset = 0; offset < events.size(); offset += 4) {
            log.record("info", "video", "recovery_events",
                       {{"events", QJsonArray::fromVariantList(events.mid(offset, 4))},
                        {"omitted", offset == 0 ? batch.value("omitted").toLongLong() : 0},
                        {"observedUs", batch.value("observedUs").toLongLong()}});
        }
    };
    connect(&video, &VideoReceiver::statusChanged, this, [&video, recovery_events] {
        // stop 已 join 工作线程，此时冲刷尾部事件，避免正常退出丢掉最后一次重置。
        if (!video.running()) recovery_events();
    });
    auto* heartbeat_timer = new QTimer(this);
    heartbeat_timer->setTimerType(Qt::PreciseTimer);
    heartbeat_timer->setInterval(VideoTimingStatistics::kHeartbeatIntervalMs);
    heartbeat_timings_.heartbeat(monotonicUs());
    connect(heartbeat_timer, &QTimer::timeout, this,
            [this] { heartbeat_timings_.heartbeat(monotonicUs()); });
    heartbeat_timer->start();
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
                    {"connecting", session.connecting()},
                    {"networkReady", session.networkReady()},
                    {"error", session.error()},
                    {"configuration", QJsonObject::fromVariantMap(session.configuration())}});
    };
    connect(&session, &MatchSession::statusChanged, this, session_state);
    connect(&session, &MatchSession::configurationChanged, this, session_state);
    connect(session.serialController(), &SerialController::statusChanged, this,
            [&, previous_state = QString{}, previous_error = QString{}]() mutable {
                const auto state = session.serialController()->state();
                const auto error = session.serialController()->error();
                // 高频发送结果只进每秒聚合快照，不因转发状态交替逐帧写文件。
                if (state == previous_state && error == previous_error) return;
                previous_state = state;
                previous_error = error;
                log.record("info", "serial", "state_changed", {{"state", state}, {"error", error}});
            });
    connect(session.serialController(), &SerialController::configurationChanged, this, [&] {
        log.record("info", "serial", "configuration_saved",
                   {{"configuration",
                     QJsonObject::fromVariantMap(session.serialController()->configuration())}});
    });
    connect(&window, &QWindow::visibilityChanged, this, [&](QWindow::Visibility visibility) {
        log.record("info", "window", "visibility_changed", {{"visibility", int(visibility)}});
    });
    connect(&window, &QQuickWindow::sceneGraphError, this,
            [&](QQuickWindow::SceneGraphError code, const QString& message) {
                log.record("error", "graphics", "scene_graph_error",
                           {{"code", int(code)}, {"message", message}});
            });
    if (auto* presenter = qobject_cast<PresentationWindow*>(&window)) {
        connect(presenter, &PresentationWindow::presentationErrorChanged, this, [&, presenter] {
            const auto error = presenter->presentationError();
            log.record(error.isEmpty() ? "info" : "error", "graphics", "presentation_state",
                       {{"error", error}, {"independent", presenter->independentPresentation()}});
        });
    }
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [&, recovery_events] {
        controller.setLogLocation(log.rootDirectory(), log.error());
        recovery_events();
        auto* viewport = window.findChild<VideoView*>("rmulVideo");
        const auto heartbeat = heartbeat_timings_.snapshot(monotonicUs()).value("guiHeartbeat");
        log.record(
            "info", "diagnostics", "runtime_snapshot",
            {{"graphics", window.property("graphicsBackend").toString()},
             {"presentationError", window.property("presentationError").toString()},
             {"independentPresentation", window.property("independentPresentation").toBool()},
             {"windowWidth", window.width()},
             {"windowHeight", window.height()},
             {"screenHz", window.screen() ? window.screen()->refreshRate() : 0},
             {"gui",
              QJsonObject{{"heartbeat", QJsonObject::fromVariantMap(heartbeat.toMap())},
                          {"heartbeatIntervalMs", VideoTimingStatistics::kHeartbeatIntervalMs}}},
             {"video", QJsonObject::fromVariantMap(video.diagnostics())},
             {"output", viewport ? QJsonObject::fromVariantMap(viewport->presentationDiagnostics())
                                 : QJsonObject{}},
             {"mqtt", QJsonObject::fromVariantMap(pipeline.diagnostics())},
             {"serial", QJsonObject::fromVariantMap(session.serialController()->diagnostics())}});
    });
    app_state();
    video_state();
    network_state();
    session_state();
    timer->start();
}
