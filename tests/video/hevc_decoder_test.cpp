#include "video/hevc_decoder.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTest>

/** 使用真实 HEVC 访问单元验证后端选择、回退和参考帧生命周期。 */
class HevcDecoderTest : public QObject {
    Q_OBJECT
   private slots:
    void retainedPixelsSurviveDecoderResetAndDestruction() {
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto unit = file.readAll();
        VideoFrame retained;
        QImage expected;
        {
            HevcDecoder decoder("cpu");
            retained = decoder.decode(unit, 100, 90, 12);
            QVERIFY(!retained.isNull());
            QVERIFY(retained.image.isNull());
            QVERIFY(retained.pixels);
            expected = retained.toImage();
            decoder.reset();
            for (int i = 0; i < 8; ++i) QVERIFY(!decoder.decode(unit).isNull());
        }
        QCOMPARE(retained.toImage(), expected);
        QCOMPARE(retained.size(), QSize(1920, 1080));
        QCOMPARE(retained.first_packet_us, 90LL);
        QCOMPARE(retained.frame_id, 12);
    }
    void softwareStageCostsAreSeparate() {
        HevcDecoder decoder("cpu");
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto frame = decoder.decode(file.readAll(), 123456, 120000, 65535);
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.assembled_us, 123456LL);
        QCOMPARE(frame.first_packet_us, 120000LL);
        QCOMPARE(frame.frame_id, 65535);
        const auto costs = decoder.timings();
        QVERIFY(costs.total_us >= costs.codec_us);
        QVERIFY(costs.codec_us >= 0);
        QCOMPARE(costs.convert_us, -1LL);
        QCOMPARE(costs.download_us, -1LL);
        decoder.decode({});
        QCOMPARE(decoder.timings().codec_us, -1LL);
    }
    void deviceInventoryDistinguishesInitializationFromFrames() {
        const auto devices = HevcDecoder::probeDevices();
        QVERIFY(!devices.isEmpty());
        bool cpu = false;
        for (const auto& value : devices) {
            const auto entry = value.toMap();
            qInfo().noquote() << "HEVC device:" << entry;
            QVERIFY(!entry.value("key").toString().isEmpty());
            QVERIFY(!entry.value("label").toString().isEmpty());
            QVERIFY(!entry.value("hardwareActive").toBool());
            if (entry.value("key") == "cpu") cpu = entry.value("available").toBool();
            if (!entry.value("available").toBool())
                QVERIFY(!entry.value("reason").toString().isEmpty());
        }
        QVERIFY(cpu);
    }
    void missingSavedDeviceFallsBackWithoutSelectingAnotherGpu() {
        HevcDecoder decoder("selected", "cuda:00000000000000000000000000000000");
        QCOMPARE(decoder.backend(), QString("CPU"));
        QVERIFY(decoder.fallbackReason().contains("不存在"));
    }
    void selectableDevicesDecodeActualFrames() {
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto unit = file.readAll();
        for (const auto& value : HevcDecoder::probeDevices()) {
            const auto entry = value.toMap();
            if (entry.value("key") == "cpu" || !entry.value("available").toBool()) continue;
            HevcDecoder decoder("selected", entry.value("key").toString());
            VideoFrame frame;
            for (int i = 0; i < 3; ++i) frame = decoder.decode(unit, 234567 + i, 230000 + i);
            QVERIFY2(!frame.isNull(), qPrintable(decoder.error()));
            QCOMPARE(frame.assembled_us, 234569LL);
            QCOMPARE(frame.first_packet_us, 230002LL);
            if (decoder.hardwareActive()) {
                QCOMPARE(decoder.backend(), entry.value("backend").toString());
                QCOMPARE(decoder.timings().download_us, -1LL);
                qInfo().noquote() << "Selected hardware confirmed:" << entry.value("label");
            } else {
                // 初始化通过也可能拒绝实际 profile，降级必须可诊断，不静默宣称 GPU 已用。
                QVERIFY(!decoder.fallbackReason().isEmpty());
                qInfo().noquote() << "Selected device fell back:" << decoder.fallbackReason();
            }
        }
    }
    void decoding_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QSize>("size");
        QTest::newRow("320p") << QString(PIP_VIDEO_FIXTURE) << QSize(320, 180);
        QTest::newRow("1080p") << QString(PIP_VIDEO_HD_FIXTURE) << QSize(1920, 1080);
    }
    void decoding() {
        QFETCH(QString, path);
        QFETCH(QSize, size);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto unit = file.readAll();
        HevcDecoder software("cpu");
        HevcDecoder automatic;
        const auto reference = software.decode(unit);
        QVERIFY2(!reference.isNull(), qPrintable(software.error()));
        QCOMPARE(reference.size(), size);
        VideoFrame actual;
        for (int i = 0; i < 3 && actual.isNull(); ++i) actual = automatic.decode(unit);
        QVERIFY2(!actual.isNull(), qPrintable(automatic.error()));
        QCOMPARE(actual.size(), size);
        QCOMPARE(actual.pixel_aspect, reference.pixel_aspect);
        // 硬件的 YUV 重建允许少量舍入差异；对照覆盖下载后的矩阵、范围和通道顺序。
        const auto reference_image = reference.toImage();
        const auto actual_image = actual.toImage();
        QVERIFY(!reference_image.isNull());
        QVERIFY(!actual_image.isNull());
        qint64 difference = 0;
        for (int y = 0; y < size.height(); ++y) {
            const auto* expected = reference_image.constScanLine(y);
            const auto* observed = actual_image.constScanLine(y);
            for (int x = 0; x < size.width() * 4; ++x)
                difference += qAbs(int(expected[x]) - int(observed[x]));
        }
        QVERIFY(double(difference) / (size.width() * size.height() * 4) < 3.0);
        const auto backend = automatic.backend();
        automatic.reset();
        QCOMPARE(automatic.backend().remove('?'), QString(backend).remove('?'));
        QVERIFY(!automatic.decode(unit).isNull());
        // 只记录调用耗时，测试机负载与重复 IDR 不构成真实 60 FPS 验收。
        for (auto* decoder : {&software, &automatic}) {
            QElapsedTimer elapsed;
            elapsed.start();
            for (int i = 0; i < 30; ++i) QVERIFY(!decoder->decode(unit).isNull());
            qInfo().noquote() << size << decoder->backend() << decoder->device() << "mean ms"
                              << elapsed.nsecsElapsed() / 30000000.0
                              << "fallback:" << decoder->fallbackReason();
        }
    }
    void invalidBackendFallsBackWithoutRepeatedProbing() {
        HevcDecoder decoder("unavailable-backend");
        QCOMPARE(decoder.backend(), QString("CPU"));
        QVERIFY(!decoder.hardwareActive());
        QVERIFY(!decoder.fallbackReason().isEmpty());
        QVERIFY(decoder.error().isEmpty());
        const auto reason = decoder.fallbackReason();
        decoder.reset();
        QCOMPARE(decoder.fallbackReason(), reason);
        QFile file(PIP_VIDEO_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!decoder.decode(file.readAll()).isNull());
    }
    void unavailableDeviceFallsBackAndStillDecodes() {
#ifdef Q_OS_WIN
        HevcDecoder decoder("d3d11va", "2147483647");
#else
        HevcDecoder decoder("vaapi", "/dev/dri/pip-test-nonexistent-device");
#endif
        QCOMPARE(decoder.backend(), QString("CPU"));
        QVERIFY(!decoder.fallbackReason().isEmpty());
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!decoder.decode(file.readAll()).isNull());
    }
    void malformedAccessUnitDoesNotDisableHardware() {
        HevcDecoder decoder;
        const auto selected = decoder.backend();
        QVERIFY(decoder.decode("not a HEVC access unit").isNull());
        QCOMPARE(decoder.backend(), selected);
        decoder.reset();
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY2(!decoder.decode(file.readAll()).isNull(), qPrintable(decoder.error()));
    }
    void actualHardwareOutput() {
        HevcDecoder decoder;
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto unit = file.readAll();
        for (int i = 0; i < 3; ++i) decoder.decode(unit);
        if (!decoder.hardwareActive()) QSKIP(qPrintable(decoder.fallbackReason()));
        QVERIFY(decoder.backend() != "CPU");
        qInfo().noquote() << "Confirmed hardware frame:" << decoder.backend() << decoder.device();
    }
};
QTEST_GUILESS_MAIN(HevcDecoderTest)
#include "hevc_decoder_test.moc"
