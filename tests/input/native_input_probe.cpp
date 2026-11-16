#include <QGuiApplication>
#include <QJsonDocument>
#include <QTest>
#include <QWindow>
#include <thread>

#include "platform/native_input.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/** 手动原生窗口专项；注入 OS 输入仅验证适配链路，不测硬件轮询率或真实端到端延迟。
 * 不注册为默认 CTest，需独占前台窗口；无 MQTT，测试期间不控制机器人。
 */
class NativeInputProbe : public QObject {
    Q_OBJECT
   private slots:
    void nativeBackendStartsAndReleases() {
        const auto platform = QGuiApplication::platformName();
        if (platform != "windows" && platform != "xcb" && !platform.startsWith("wayland"))
            QSKIP("需要原生桌面平台");
        QWindow window;
        window.setTitle("PIP input backend lifecycle (no MQTT)");
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int i = 0; i < 3; ++i) {
            auto state = std::make_shared<InputState>();
            {
                NativeInput input(window, state);
                state->request(InputState::Mode::Test, false, 1, false, 200);
                QTRY_VERIFY_WITH_TIMEOUT(state->diagnostics(InputState::nowUs())
                                             .value("backend")
                                             .toString()
                                             .contains(" / "),
                                         5000);
                qInfo() << state->diagnostics(InputState::nowUs()).value("backend");
                QVERIFY(
                    state->diagnostics(InputState::nowUs()).value("error").toString().isEmpty());
            }
            QVERIFY(!state->sample(InputState::nowUs()).valid);
        }
    }
    void rawInputContinuesWhenGuiIsBlocked() {
#ifndef Q_OS_WIN
        QSKIP("本专项注入器使用 Windows SendInput；Linux 后端须独立桌面验收");
#else
        if (QGuiApplication::platformName() != "windows") QSKIP("需要原生 Windows 平台");
        QWindow window;
        window.setTitle("PIP native input integration test (no MQTT)");
        window.resize(600, 400);
        window.show();
        window.requestActivate();
        SetForegroundWindow(reinterpret_cast<HWND>(window.winId()));
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_COMPARE(GetForegroundWindow(), reinterpret_cast<HWND>(window.winId()));
        auto state = std::make_shared<InputState>();
        NativeInput native(window, state);
        state->request(InputState::Mode::Test, true, 1, false, 200);
        QTRY_VERIFY2(state->sample(InputState::nowUs()).locked,
                     qPrintable(QString::fromUtf8(
                         QJsonDocument::fromVariant(state->diagnostics(InputState::nowUs()))
                             .toJson(QJsonDocument::Compact))));
        QTest::qWait(20);
        auto keys = [](bool down) {
            INPUT inputs[3]{};
            const WORD scans[] = {17, 30, 42};
            for (int i = 0; i < 3; ++i) {
                inputs[i].type = INPUT_KEYBOARD;
                inputs[i].ki.wScan = scans[i];
                inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
            }
            return SendInput(3, inputs, sizeof(INPUT));
        };
        QCOMPARE(keys(true), UINT(3));
        QTRY_COMPARE(state->sample(InputState::nowUs()).keyboard, quint16(21));
        QCOMPARE(keys(false), UINT(3));
        QThread::msleep(30);
        QCOMPARE(state->sample(InputState::nowUs()).keyboard, quint16(0));
        const auto before = state->diagnostics(InputState::nowUs())
                                .value("mouse")
                                .toMap()
                                .value("count")
                                .toULongLong();
        std::jthread mouse([] {
            for (int i = 0; i < 50; ++i) {
                INPUT input{};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_MOVE;
                input.mi.dx = 2;
                SendInput(1, &input, sizeof(INPUT));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        QThread::msleep(100);
        mouse.join();
        const auto after = state->diagnostics(InputState::nowUs());
        qInfo() << after;
        QVERIFY(after.value("mouse").toMap().value("count").toULongLong() > before + 20);
        QVERIFY(state->sample(InputState::nowUs()).valid);
        // 切换到其他原生窗口，GUI 不派发事件时原生焦点检查也必须释放约束。
        QWindow other;
        other.setTitle("PIP input focus target");
        other.resize(200, 150);
        other.show();
        other.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QThread::msleep(15);
        QVERIFY(!state->sample(InputState::nowUs()).valid);
        state->interrupt();
        QThread::msleep(10);
        RECT clip{};
        QVERIFY(GetClipCursor(&clip));
        QVERIFY(clip.right - clip.left > window.width());
#endif
    }
};
QTEST_MAIN(NativeInputProbe)
#include "native_input_probe.moc"
