#include "input/input_state.h"

#include <QTest>

#include "input/input_packet.h"

/** 使用独立固定输入向量验证多键释放、累计移动和有界统计，不访问真实设备。 */
class InputStateTest : public QObject {
    Q_OBJECT
   private slots:
    void localChecklistKeepsFastComboAndMotionEvidence() {
        InputState state;
        state.request(InputState::Mode::Test, false, 1, false, 200);
        state.capture(true, false, 1000000);
        state.key(1, 42, true, 1000100);
        state.key(2, 17, true, 1000200);
        state.key(1, 42, false, 1000300);
        QVERIFY(!state.diagnostics(1000300).value("testComboReleased").toBool());
        state.key(2, 17, false, 1000400);
        // 整个组合操作发生在一次 30Hz GUI 快照内，不能依赖界面碰巧看到按住状态。
        QVERIFY(state.diagnostics(1000500).value("testComboReleased").toBool());
        state.motion(5, 0, 120, 1000600);
        state.motion(-5, 0, -120, 1000700);
        const auto evidence = state.diagnostics(1000800);
        QVERIFY(evidence.value("testMouseMoved").toBool());
        QVERIFY(evidence.value("testWheelMoved").toBool());
        state.resetStatistics();
        const auto reset = state.diagnostics(1000900);
        QVERIFY(!reset.value("testComboReleased").toBool());
        QVERIFY(!reset.value("testMouseMoved").toBool());
        QVERIFY(!reset.value("testWheelMoved").toBool());
    }

    void focusLossCannotCompleteAComboRelease() {
        InputState state;
        state.request(InputState::Mode::Test, false, 1, false, 200);
        state.capture(true, false, 1000000);
        state.key(1, 42, true, 1000100);
        state.key(1, 17, true, 1000200);
        state.capture(false, false, 1000300);
        state.key(1, 42, false, 1000400);
        state.key(1, 17, false, 1000500);
        state.capture(true, false, 1000600);
        QVERIFY(!state.diagnostics(1000700).value("testComboReleased").toBool());
    }

    void testCoverageSurvivesReleaseAndResetClearsCoverageOnly() {
        InputState state;
        state.request(InputState::Mode::Test, false, 1, false, 200);
        state.capture(true, false, 1000000);
        state.key(1, 17, true, 1000100);
        state.key(1, 17, false, 1000200);
        QVERIFY(state.diagnostics(1000300).value("testedKeys").toList().contains(17));
        state.key(1, 30, true, 1000400);
        state.resetStatistics();
        QVERIFY(state.diagnostics(1000500).value("testedKeys").toList().isEmpty());
        QCOMPARE(state.sample(1000500).keyboard, quint16(4));
    }
    void hudNamesListEveryHeldPhysicalKey() {
        InputState state;
        state.key(1, 17, true, 1000000);
        state.key(1, 30, true, 1000000);
        state.key(1, 42, true, 1000000);
        state.key(1, 272, true, 1000000);
        auto diagnostics = state.diagnostics(1000100);
        QCOMPARE(diagnostics.value("pressedKeys").toStringList(),
                 QStringList({"W", "A", "LShift"}));
        QCOMPARE(diagnostics.value("pressedMouse").toStringList(), QStringList({"左键"}));
        state.key(1, 17, false, 1000200);
        state.key(1, 30, false, 1000200);
        state.key(1, 42, false, 1000200);
        QVERIFY(state.diagnostics(1000300).value("pressedKeys").toStringList().isEmpty());
    }
    void revokedGenerationCannotWriteAfterRelease() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.capture(true, true, InputState::nowUs());
        const auto sample = state.sample(InputState::nowUs());
        state.interrupt();
        bool called = false;
        QVERIFY(!state.submitCurrent(sample, [&] {
            called = true;
            return true;
        }));
        QVERIFY(!called);
    }
    void offlineSubmissionRejectsFreshCapture() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1, false, 200);
        // submitCurrent 使用真实单调时钟检查健康；固定历史时间不能证明传输门禁有效。
        state.capture(true, true, InputState::nowUs());
        QVERIFY(state.submitCurrent(state.sample(InputState::nowUs()), [] { return true; }));
        state.transport(false);
        state.capture(true, true, InputState::nowUs());
        bool called = false;
        QVERIFY(!state.submitCurrent(state.sample(InputState::nowUs()), [&] {
            called = true;
            return true;
        }));
        QVERIFY(!called);
    }
    void devicesAndModifiersRemainIndependent() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.capture(true, true, 1000000);
        state.key(1, 42, true, 1000100);
        state.key(2, 54, true, 1000100);
        state.key(1, 42, false, 1000200);
        QCOMPARE(state.sample(1000300).keyboard, quint16(16));
        state.removeDevice(2);
        QCOMPARE(state.sample(1000400).keyboard, quint16(0));
    }
    void oldHeldKeysAreSuppressedPerDevice() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.capture(true, true, 1000000);
        state.key(1, 17, true, 1000100);
        state.key(2, 17, true, 1000200);
        state.capture(false, false, 1000300);
        state.capture(true, true, 1000400);
        state.key(1, 17, false, 1000500);
        QCOMPARE(state.sample(1000600).keyboard, quint16(0));
        state.key(1, 17, true, 1000700);
        QCOMPARE(state.sample(1000800).keyboard, quint16(1));
        state.key(1, 17, false, 1000900);
        QCOMPARE(state.sample(1001000).keyboard, quint16(0));
    }
    void healthTimeoutDiscardsMovementBeforeRecovery() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.capture(true, true, 1000000);
        state.motion(123, 456, 120, 1000100);
        const auto generation = state.sample(1000200).generation;
        QVERIFY(!state.sample(1025001).valid);
        state.capture(true, true, 1040000);
        const auto resumed = state.sample(1040001);
        QVERIFY(resumed.generation > generation);
        QCOMPARE(resumed.x, 0.0);
        QCOMPARE(state.diagnostics(1040001).value("healthTimeouts").toULongLong(), quint64(1));
        state.request(InputState::Mode::Off, false, 1, false, 200);
        state.capture(false, false, 2000000);
        QCOMPARE(state.diagnostics(2001000).value("healthTimeouts").toULongLong(), quint64(1));
        QCOMPARE(state.diagnostics(2001000).value("heartbeatAgeMs").toDouble(), 1.0);
    }
    void mouseCountsPacketsRatherThanAxesAndButtons() {
        InputState state;
        state.request(InputState::Mode::Test, false, 1, false, 200);
        state.capture(true, false, 1000000);
        for (int i = 0; i < 100; ++i) {
            const auto time = 1000100 + i * 1000;
            state.mouseReport(time);
            state.motion(1, 2, 0, time);
            state.key(1, 272, i % 2 == 0, time);
            state.capture(true, false, time);
        }
        const auto mouse = state.diagnostics(1100000).value("mouse").toMap();
        QCOMPARE(mouse.value("count").toULongLong(), quint64(100));
        QCOMPARE(mouse.value("activeHz").toDouble(), 1000.0);
        QCOMPARE(state.diagnostics(2200000).value("mouse").toMap().value("hz").toDouble(), 0.0);
    }
    void packetAccumulatesFractionalSensitivityAndCommitsOnce() {
        InputPacket packet;
        InputState::Sample sample;
        sample.mode = InputState::Mode::Control;
        sample.valid = true;
        sample.generation = 1;
        sample.sensitivity = .25;
        rm_client_to_server::KeyboardMouseControl message;
        for (int i = 1; i <= 4; ++i) {
            sample.x = i;
            sample.y = i;
            QVERIFY(packet.prepare(sample, message));
            QCOMPARE(message.mouse_x(), i == 4 ? 1 : 0);
            QCOMPARE(message.mouse_y(), i == 4 ? -1 : 0);
            packet.commit();
        }
        packet.prepare(sample, message);
        QCOMPARE(message.mouse_x(), 0);
        sample.x += 8;
        packet.prepare(sample, message);  // 本地写出失败，不 commit；下次仍包含这段运动。
        packet.prepare(sample, message);
        QCOMPARE(message.mouse_x(), 2);
    }
    void testModeOnlyReleasesPreviousControlOnce() {
        InputPacket packet;
        InputState::Sample sample;
        sample.mode = InputState::Mode::Test;
        sample.valid = true;
        sample.keyboard = 65535;
        rm_client_to_server::KeyboardMouseControl message;
        QVERIFY(!packet.prepare(sample, message));
        sample.mode = InputState::Mode::Control;
        QVERIFY(packet.prepare(sample, message));
        packet.commit();
        sample.mode = InputState::Mode::Test;
        QVERIFY(packet.prepare(sample, message));
        QCOMPARE(message.keyboard_value(), quint32(0));
        packet.commit();
        QVERIFY(!packet.prepare(sample, message));
    }
    void offlineCaptureKeepsKeysAndReconnectDiscardsOnlyMovement() {
        InputState state;
        state.transport(false);
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.capture(true, true, 1000000);
        state.key(1, 17, true, 1000100);
        state.motion(123, 456, 120, 1000200);
        const auto offline = state.sample(1000300);
        QVERIFY(offline.valid);
        QCOMPARE(state.requested().first, InputState::Mode::Control);
        QCOMPARE(offline.keyboard, quint16(1));
        QCOMPARE(offline.x, 123.0);
        state.transport(true);
        const auto online = state.sample(1000400);
        QVERIFY(online.valid);
        QVERIFY(online.generation > offline.generation);
        QCOMPARE(online.keyboard, quint16(1));
        QCOMPARE(online.x, 0.0);
        QCOMPARE(online.y, 0.0);
        QCOMPARE(online.wheel, 0.0);
        state.transport(false);
        QVERIFY(state.sample(1000500).valid);
        QCOMPARE(state.sample(1000500).keyboard, quint16(1));
    }
    void escapeInterruptsWithoutGuiAndOfflineTestStillWorks() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.capture(true, true, 1000000);
        state.key(1, 1, true, 1000100);
        QVERIFY(!state.sample(1000200).valid);
        state.request(InputState::Mode::Control, true, 1, false, 200);
        state.transport(false);
        state.capture(true, true, 1000300);
        QVERIFY(state.sample(1000400).valid);
        state.request(InputState::Mode::Test, false, 1, false, 200);
        state.capture(true, false, 1000500);
        QVERIFY(state.sample(1000600).valid);
    }
    void simultaneousReleaseClearsEveryKey() {
        InputState state;
        state.request(InputState::Mode::Control, true, 1.0, false, 75);
        state.capture(true, true, 1000000);
        state.key(1, 17, true, 1000100);  // W
        state.key(1, 30, true, 1000200);  // A
        state.key(1, 42, true, 1000300);  // 左 Shift
        QCOMPARE(state.sample(1000400).keyboard, quint16(0x15));
        state.key(1, 17, false, 1000500);
        state.key(1, 30, false, 1000500);
        state.key(1, 42, false, 1000500);
        QCOMPARE(state.sample(1000600).keyboard, quint16(0));
    }
};
QTEST_GUILESS_MAIN(InputStateTest)
#include "input_state_test.moc"
