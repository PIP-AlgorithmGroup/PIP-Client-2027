#pragma once

#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMutex>
#include <QNetworkInterface>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <memory>
#include <vector>

#include "platform/background_timer_resolution.h"
#include "video/presentation_window.h"
#include "video/video_receiver.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
// Windows COM 头的 interface 宏不能泄漏到包含本工具的测试变量名。
#undef interface
#endif

namespace video_test {
/** 手动测量才启用的 GUI 事件耗时探针；普通回归完全透传。
 * 只在应用线程记录超过 2ms 的最外层 notify，最多 5000 项；结束后统一写盘。
 * 观测区间包含同步调用的 QML/信号处理与嵌套事件，不等同于纯 CPU 执行时间。
 */
class ProbeApplication : public QGuiApplication {
   public:
    ProbeApplication(int& argc, char** argv)
        : QGuiApplication(argc, argv),
          enabled_(!qEnvironmentVariableIsEmpty("PIP_VIDEO_PROBE_OUTPUT")) {}
    bool notify(QObject* receiver, QEvent* event) override {
        if (!enabled_ || QThread::currentThread() != thread() || inside_)
            return QGuiApplication::notify(receiver, event);
        inside_ = true;
        const auto began = std::chrono::steady_clock::now();
        // notify 内允许销毁接收对象及其动态 QML metaobject，必须先复制类名。
        const QByteArray class_name(receiver->metaObject()->className());
        const int type = event->type();
        const bool result = QGuiApplication::notify(receiver, event);
        const auto ended = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(ended - began);
        if (duration.count() >= 2000 && events_.size() < 5000)
            events_.append(
                QVariantMap{{"atUs", qint64(std::chrono::duration_cast<std::chrono::microseconds>(
                                                began.time_since_epoch())
                                                .count())},
                            {"durationUs", qint64(duration.count())},
                            {"type", type},
                            {"class", QString::fromLatin1(class_name)}});
        inside_ = false;
        return result;
    }
    /// 仅 GUI 线程读取；结果为有界值副本，不暴露事件或 QObject 的借用地址。
    QVariantList costlyEvents() const { return events_; }

   private:
    bool enabled_ = false, inside_ = false;
    QVariantList events_;
};

/** 手动原生性能实验：完整 QML 路径的前后台阶段与有界逐帧采样。
 * 仅设置 PIP_VIDEO_PROBE_OUTPUT 时使用；不进入普通 CTest，不改变产品策略。
 * 时间为 steady_clock 微秒。sync 回调在 GUI 阻塞期间抓取已交付帧，swap 时固定输出该快照，
 * 不把 swap 时 GUI 的更新帧冒充本次已绘制帧；重复 UI 重绘不增加视频样本。
 * 独立循环由 videoFramePresented 直接给出真实帧值；GUI/sync 时间保留为 -1。
 */
class PresentationProbe final : public QObject {
    struct Sample {
        qint64 first = -1, decoded = -1, gui = -1, requested = -1;
        qint64 began = -1, sync = -1, synced = -1, rendering = -1, rendered = -1, swap = -1;
        qint64 picked = -1, ready = -1;
        int id = -1;
    };
    struct Shared {
        QMutex mutex;
        Sample latest, pending;
        qint64 began = -1;
        int last_id = -1;
        std::vector<Sample> samples;
    };

   public:
    /** 仅 GUI 线程构造/运行/析构；window/receiver 必须晚于本工具析构。
     * 采样连接直接运行于发送信号的线程，仅访问独立共享状态；最多记录 20000 帧。
     */
    PresentationProbe(QQuickWindow& window, VideoReceiver& receiver)
        : window_(window), receiver_(receiver), shared_(std::make_shared<Shared>()) {
        const auto state = shared_;
        auto* presenter = qobject_cast<PresentationWindow*>(&window);
        const bool independent = presenter && presenter->independentPresentation();
        if (independent)
            connect(
                presenter, &PresentationWindow::videoFramePresented, this,
                [state](int id, qint64 first, qint64 decoded, qint64 picked, qint64 ready,
                        qint64 swap) {
                    QMutexLocker lock(&state->mutex);
                    Sample sample;
                    sample.id = id;
                    sample.first = first;
                    sample.decoded = decoded;
                    sample.picked = picked;
                    sample.ready = ready;
                    sample.began = state->began;
                    sample.rendering = state->pending.rendering;
                    sample.rendered = state->pending.rendered;
                    sample.swap = swap;
                    if (state->samples.size() < 20000) state->samples.push_back(sample);
                },
                Qt::DirectConnection);
        connect(&receiver, &VideoReceiver::frameChanged, this, [state, &receiver] {
            const auto frame = receiver.frame();
            QMutexLocker lock(&state->mutex);
            state->latest.first = frame.first_packet_us;
            state->latest.decoded = frame.decoded_us;
            state->latest.gui = frame.gui_received_us;
            state->latest.requested = nowUs();
            state->latest.id = frame.isNull() ? -1 : frame.frame_id;
        });
        connect(
            &window, &QQuickWindow::beforeFrameBegin, this,
            [state] {
                QMutexLocker lock(&state->mutex);
                state->began = nowUs();
            },
            Qt::DirectConnection);
        connect(
            &window, &QQuickWindow::beforeSynchronizing, this,
            [state] {
                QMutexLocker lock(&state->mutex);
                state->pending = state->latest;
                state->pending.began = state->began;
                state->pending.sync = nowUs();
            },
            Qt::DirectConnection);
        connect(
            &window, &QQuickWindow::afterSynchronizing, this,
            [state] {
                QMutexLocker lock(&state->mutex);
                state->pending.synced = nowUs();
            },
            Qt::DirectConnection);
        connect(
            &window, &QQuickWindow::beforeRendering, this,
            [state] {
                QMutexLocker lock(&state->mutex);
                state->pending.rendering = nowUs();
            },
            Qt::DirectConnection);
        connect(
            &window, &QQuickWindow::afterRendering, this,
            [state] {
                QMutexLocker lock(&state->mutex);
                state->pending.rendered = nowUs();
            },
            Qt::DirectConnection);
        connect(
            &window, &QQuickWindow::frameSwapped, this,
            [state, independent] {
                if (independent) return;
                QMutexLocker lock(&state->mutex);
                if (state->pending.id < 0 || state->pending.id == state->last_id) return;
                state->pending.swap = nowUs();
                state->last_id = state->pending.id;
                if (state->samples.size() < 20000) state->samples.push_back(state->pending);
            },
            Qt::DirectConnection);
    }
    /** 输出阶段、窗口状态、GUI 心跳和帧时间；独立遮挡程序失败时返回错误，禁止假装已遮挡。
     * stage_ms 为每阶段持续时间，环境变量可选 5000..30000ms，默认 8000ms。
     */
    QString run(const QString& output) {
        const auto stage_text = qEnvironmentVariable("PIP_VIDEO_PROBE_STAGE_MS", "8000");
        bool is_valid_stage = false;
        const int stage_ms = stage_text.toInt(&is_valid_stage);
        if (!is_valid_stage || stage_ms < 5000 || stage_ms > 30000)
            return "PIP_VIDEO_PROBE_STAGE_MS 必须为 5000..30000ms";
        std::unique_ptr<BackgroundTimerResolution> timing;
        if (qEnvironmentVariable("PIP_VIDEO_PROBE_TIMER") == "1") {
            timing = std::make_unique<BackgroundTimerResolution>();
            if (!timing->error().isEmpty()) return timing->error();
        }
        const auto cover_path = qEnvironmentVariable("PIP_VIDEO_PROBE_COVER");
        if (!QFile::exists(cover_path)) return "缺少独立遮挡程序 PIP_VIDEO_PROBE_COVER";
        window_.setGeometry(window_.screen()->availableGeometry().topLeft().x() + 30,
                            window_.screen()->availableGeometry().topLeft().y() + 30, 960, 600);
        QProcess cover;
        QEventLoop loop;
        QVariantList phases, states, heartbeats;
        QByteArray cover_messages;
        connect(&cover, &QProcess::readyReadStandardOutput, &loop,
                [&] { cover_messages += cover.readAllStandardOutput(); });
        connect(&cover, &QProcess::readyReadStandardError, &loop,
                [&] { cover_messages += cover.readAllStandardError(); });
        QString failure;
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        heartbeat.setInterval(10);
        qint64 last_tick = nowUs();
        connect(&heartbeat, &QTimer::timeout, &loop, [&] {
            const auto now = nowUs();
            heartbeats.append(QVariantMap{{"atUs", now}, {"intervalUs", now - last_tick}});
            last_tick = now;
        });
        heartbeat.start();
        // 在已修正的同一二进制里重放原先 GUI 每 500ms 系统查询，形成可交替的因果对照。
        // 仅手动实验启用，真实产品不会读取该变量；查询不修改系统接口。
        QTimer legacy_network_poll;
        legacy_network_poll.setInterval(500);
        connect(&legacy_network_poll, &QTimer::timeout, &loop,
                [] { static_cast<void>(QNetworkInterface::interfaceFromName({})); });
        const bool replay_network = qEnvironmentVariable("PIP_VIDEO_PROBE_REPLAY_NETWORK") == "1";
        if (replay_network) legacy_network_poll.start();
        QTimer observation;
        observation.setInterval(250);
        connect(&observation, &QTimer::timeout, &loop, [&] {
            QVariantMap policy;
            bool is_active = window_.isActive();
            bool is_covered = false;
#ifdef Q_OS_WIN
            // Qt isActive 在原生实验中出现父/子进程同时为 true 的缓存状态；
            // 以 OS 前台 HWND 判定失焦，并独立验证中心点属于本工具的遮挡进程。
            is_active = GetForegroundWindow() == reinterpret_cast<HWND>(window_.winId());
            if (cover.state() == QProcess::Running) {
                // Qt 全局坐标是逻辑像素；使用原生 client→screen，避免混合 DPI 下错查中心点。
                const auto handle = reinterpret_cast<HWND>(window_.winId());
                RECT client{};
                GetClientRect(handle, &client);
                POINT center{client.right / 2, client.bottom / 2};
                ClientToScreen(handle, &center);
                const auto covered_window = WindowFromPoint(center);
                DWORD process = 0;
                GetWindowThreadProcessId(covered_window, &process);
                is_covered = process == DWORD(cover.processId());
            }
            PROCESS_POWER_THROTTLING_STATE native_policy{};
            native_policy.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            if (GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &native_policy,
                                      sizeof(native_policy))) {
                policy["control"] = quint32(native_policy.ControlMask);
                policy["state"] = quint32(native_policy.StateMask);
            }
#endif
            states.append(QVariantMap{{"atUs", nowUs()},
                                      {"active", is_active},
                                      {"qtActive", window_.isActive()},
                                      {"centerCoveredByProbe", is_covered},
                                      {"screenName", window_.screen()->name()},
                                      {"screenHz", window_.screen()->refreshRate()},
                                      {"devicePixelRatio", window_.devicePixelRatio()},
                                      {"exposed", window_.isExposed()},
                                      {"visibility", int(window_.visibility())},
                                      {"timerPolicy", policy},
                                      {"coverRunning", cover.state() == QProcess::Running},
                                      {"coverExit", cover.exitCode()},
                                      {"receiver", receiver_.diagnostics()}});
        });
        observation.start();
        const auto stop_cover = [&] {
            if (cover.state() == QProcess::NotRunning) return;
            cover.terminate();
            if (!cover.waitForFinished(1000)) {
                cover.kill();
                cover.waitForFinished(1000);
            }
        };
        const auto show_cover = [&](const QRect& geometry) {
            stop_cover();
            cover.start(cover_path,
                        {QString::number(geometry.x()), QString::number(geometry.y()),
                         QString::number(geometry.width()), QString::number(geometry.height())});
            if (!cover.waitForStarted(2000)) failure = cover.errorString();
#ifdef Q_OS_WIN
            // 只允许自己启动的遮挡进程激活；不修改系统前台锁策略。
            if (cover.state() == QProcess::Running)
                AllowSetForegroundWindow(DWORD(cover.processId()));
#endif
        };
        const QStringList names{"foreground", "unfocused", "covered",
                                "minimized",  "recovered", "gui-blocked"};
        for (int i = 0; i <= names.size(); ++i) {
            QTimer::singleShot(i * stage_ms, &loop, [&, i] {
                if (i == names.size()) {
                    loop.quit();
                    return;
                }
                const auto name = names[i];
                if (name == "foreground" || name == "recovered") {
                    stop_cover();
                    window_.showNormal();
                    window_.raise();
                    window_.requestActivate();
                } else if (name == "unfocused") {
                    const auto available = window_.screen()->availableGeometry();
                    show_cover({available.right() - 200, available.top(), 180, 160});
                } else if (name == "covered") {
                    show_cover(window_.screen()->availableGeometry());
                } else if (name == "minimized") {
                    window_.showMinimized();
                }
                phases.append(QVariantMap{{"name", name}, {"atUs", nowUs()}});
                if (name == "gui-blocked") {
                    // 注入一次已知停顿，只阻塞本测试 GUI；发送/解码线程仍按原节奏工作。
                    QTimer::singleShot(stage_ms / 2, &loop, [&] {
                        phases.append(QVariantMap{{"name", "block-start"}, {"atUs", nowUs()}});
                        QThread::msleep(250);
                        phases.append(QVariantMap{{"name", "block-end"}, {"atUs", nowUs()}});
                    });
                }
                if (!failure.isEmpty()) loop.quit();
            });
        }
        loop.exec();
        const auto ended = nowUs();
        stop_cover();
        window_.showNormal();
        window_.requestActivate();
        QVariantList frames;
        {
            QMutexLocker lock(&shared_->mutex);
            for (const auto& s : shared_->samples)
                frames.append(QVariantMap{{"firstUs", s.first},
                                          {"decodedUs", s.decoded},
                                          {"guiUs", s.gui},
                                          {"requestedUs", s.requested},
                                          {"beginUs", s.began},
                                          {"syncUs", s.sync},
                                          {"syncedUs", s.synced},
                                          {"renderingUs", s.rendering},
                                          {"renderedUs", s.rendered},
                                          {"swapUs", s.swap},
                                          {"pickedUs", s.picked},
                                          {"readyUs", s.ready},
                                          {"frameId", s.id}});
        }
        const QVariantMap result{
            {"phases", phases},
            {"endedUs", ended},
            {"frames", frames},
            {"windowStates", states},
            {"heartbeats", heartbeats},
            {"timerGuard", bool(timing)},
            {"stageMs", stage_ms},
            {"independentRhi", window_.property("independentPresentation")},
            {"failure", failure},
            {"costlyGuiEvents", static_cast<ProbeApplication*>(qApp)->costlyEvents()},
            {"replayNetworkPoll", replay_network},
            {"coverMessages", QString::fromUtf8(cover_messages)}};
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact)) < 0)
            return file.errorString();
        return failure;
    }

   private:
    static qint64 nowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    QQuickWindow& window_;
    VideoReceiver& receiver_;
    std::shared_ptr<Shared> shared_;
};
}  // namespace video_test
