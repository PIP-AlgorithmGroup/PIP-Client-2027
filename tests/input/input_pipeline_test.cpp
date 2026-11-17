#include <QTest>
#include <QThread>
#include <atomic>
#include <chrono>
#include <thread>

#include "client_to_server.pb.h"
#include "communication/data_pipeline.h"
#include "input/input_state.h"
#include "support/mqtt_test_broker.h"

namespace {
// 对端也放在独立事件线程，避免 GUI 停顿时只证明 TCP 缓冲仍在接受字节。
class BrokerHost {
   public:
    BrokerHost() {
        context_.moveToThread(&thread_);
        thread_.start();
        QMetaObject::invokeMethod(
            &context_,
            [&] {
                broker_ = new TestBroker;
                broker_->listen(QHostAddress::LocalHost);
                port = broker_->serverPort();
            },
            Qt::BlockingQueuedConnection);
    }
    ~BrokerHost() {
        QMetaObject::invokeMethod(
            &context_, [&] { delete broker_; }, Qt::BlockingQueuedConnection);
        thread_.quit();
        thread_.wait();
    }
    QList<QPair<QString, QByteArray>> publications() {
        QList<QPair<QString, QByteArray>> result;
        QMetaObject::invokeMethod(
            &context_, [&] { result = broker_->received_publications; },
            Qt::BlockingQueuedConnection);
        return result;
    }
    void drop() {
        QMetaObject::invokeMethod(
            &context_, [&] { broker_->dropConnection(); }, Qt::BlockingQueuedConnection);
    }
    quint16 port = 0;

   private:
    QThread thread_;
    QObject context_;
    TestBroker* broker_ = nullptr;
};
}  // namespace

/** 使用实际 MQTT 编解码及独立对端验证 GUI 停顿、静止周期发送与失效释放。 */
class InputPipelineTest : public QObject {
    Q_OBJECT
   private slots:
    void continuousPublicationDoesNotDependOnGui_data() {
        QTest::addColumn<int>("frequency");
        QTest::newRow("200Hz") << 200;
        QTest::newRow("75Hz") << 75;
    }
    void continuousPublicationDoesNotDependOnGui() {
        QFETCH(int, frequency);
        BrokerHost broker;
        QVERIFY(broker.port);
        DataPipeline pipeline;
        auto state = std::make_shared<InputState>();
        pipeline.setKeyboardMouseSource(state);
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.port;
        settings.client_id = "input-test";
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        state->request(InputState::Mode::Control, true, 1, false, frequency);
        std::atomic<bool> focus{true};
        std::jthread source([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                state->capture(focus, focus, InputState::nowUs());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        QTRY_VERIFY(state->sample(InputState::nowUs()).valid);
        state->key(1, 17, true, InputState::nowUs());
        state->key(1, 30, true, InputState::nowUs());
        state->motion(123, 456, 120, InputState::nowUs());
        QTRY_VERIFY(broker.publications().size() >= 3);
        const auto before = broker.publications().size();
        QThread::msleep(250);  // 明确不派发 GUI 事件，输入和 MQTT/对端必须继续推进。
        const auto after = broker.publications();
        QVERIFY(after.size() - before >= (frequency == 200 ? 30 : 10));
        qInfo() << frequency << "Hz, GUI blocked 250ms, received" << after.size() - before;
        qint64 sum_x = 0, sum_y = 0;
        for (const auto& publication : after) {
            QCOMPARE(publication.first, QString("KeyboardMouseControl"));
            rm_client_to_server::KeyboardMouseControl message;
            QVERIFY(message.ParseFromArray(publication.second.data(), publication.second.size()));
            sum_x += message.mouse_x();
            sum_y += message.mouse_y();
        }
        QCOMPARE(sum_x, qint64(123));
        QCOMPARE(sum_y, qint64(-456));
        state->key(1, 17, false, InputState::nowUs());
        state->key(1, 30, false, InputState::nowUs());
        QTest::qWait(40);
        auto latest = broker.publications().last().second;
        rm_client_to_server::KeyboardMouseControl released;
        QVERIFY(released.ParseFromArray(latest.data(), latest.size()));
        QCOMPARE(released.keyboard_value(), quint32(0));
        // 25ms 健康保护可在系统繁忙时额外释放；每代仅一次由 InputPacket 的确定性测试覆盖。
        focus = false;
        QTest::qWait(50);
        const auto stopped = broker.publications().size();
        QTest::qWait(40);
        QCOMPARE(broker.publications().size(), stopped);
        QVERIFY(state->diagnostics(InputState::nowUs()).value("neutralCount").toULongLong() >= 1);
        state->request(InputState::Mode::Test, true, 1, false, frequency);
        focus = true;
        QTest::qWait(50);
        QCOMPARE(broker.publications().size(), stopped);
        source.request_stop();
        source.join();
        pipeline.stop();
    }
    void disconnectKeepsCaptureAndReconnectSendsOnlyFreshMovement() {
        BrokerHost broker;
        DataPipeline pipeline;
        auto state = std::make_shared<InputState>();
        pipeline.setKeyboardMouseSource(state);
        MqttSettings settings;
        settings.host = "127.0.0.1";
        settings.port = broker.port;
        settings.client_id = "drop-test";
        QVERIFY(pipeline.start(settings));
        QTRY_COMPARE(pipeline.connectionState(), QString("ready"));
        state->request(InputState::Mode::Control, true, 1, false, 200);
        std::jthread source([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                state->capture(true, true, InputState::nowUs());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        QTRY_VERIFY(state->sample(InputState::nowUs()).valid);
        state->key(1, 17, true, InputState::nowUs());
        broker.drop();
        QThread::msleep(30);
        // 断线只撤销传输，采集线程继续续期；系统调度可超过 25ms，等待新心跳而非假定硬实时。
        // 传输切换保持按键的契约由 input_state 固定时间测试验证，不与健康超时的抑制混淆。
        QTRY_VERIFY(state->sample(InputState::nowUs()).valid);
        state->motion(999, 888, 120, InputState::nowUs());
        // 离线松开也必须更新本地状态；长时间重连期间健康保护可以因调度迟到抑制按住键。
        // 持续按住跨传输代次的精确语义由 input_state 固定时间测试覆盖。
        state->key(1, 17, false, InputState::nowUs());
        const auto stopped = broker.publications().size();
        QThread::msleep(50);
        QCOMPARE(broker.publications().size(), stopped);
        QTRY_COMPARE_WITH_TIMEOUT(pipeline.connectionState(), QString("ready"), 5000);
        QTRY_VERIFY(broker.publications().size() > stopped);
        rm_client_to_server::KeyboardMouseControl resumed;
        const auto first = broker.publications().at(stopped).second;
        QVERIFY(resumed.ParseFromArray(first.data(), first.size()));
        QCOMPARE(resumed.keyboard_value(), quint32(0));
        QCOMPARE(resumed.mouse_x(), 0);
        QCOMPARE(resumed.mouse_y(), 0);
        state->key(1, 17, true, InputState::nowUs());
        QTRY_VERIFY([&] {
            const auto latest = broker.publications().last().second;
            rm_client_to_server::KeyboardMouseControl message;
            return message.ParseFromArray(latest.data(), latest.size()) &&
                   message.keyboard_value() == 1;
        }());
        source.request_stop();
        source.join();
        pipeline.stop();
    }
};
QTEST_GUILESS_MAIN(InputPipelineTest)
#include "input_pipeline_test.moc"
