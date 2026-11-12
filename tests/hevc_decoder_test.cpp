#include "video/hevc_decoder.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTest>

/** 使用真实 HEVC 访问单元验证后端选择、回退和参考帧生命周期。 */
class HevcDecoderTest : public QObject {
    Q_OBJECT
   private slots:
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
        QVERIFY2(!reference.image.isNull(), qPrintable(software.error()));
        QCOMPARE(reference.image.size(), size);
        VideoFrame actual;
        for (int i = 0; i < 3 && actual.image.isNull(); ++i) actual = automatic.decode(unit);
        QVERIFY2(!actual.image.isNull(), qPrintable(automatic.error()));
        QCOMPARE(actual.image.size(), size);
        QCOMPARE(actual.pixel_aspect, reference.pixel_aspect);
        // 硬件的 YUV 重建允许少量舍入差异；对照覆盖下载后的矩阵、范围和通道顺序。
        qint64 difference = 0;
        for (int y = 0; y < size.height(); ++y) {
            const auto* expected = reference.image.constScanLine(y);
            const auto* observed = actual.image.constScanLine(y);
            for (int x = 0; x < size.width() * 4; ++x)
                difference += qAbs(int(expected[x]) - int(observed[x]));
        }
        QVERIFY(double(difference) / (size.width() * size.height() * 4) < 3.0);
        const auto backend = automatic.backend();
        automatic.reset();
        QCOMPARE(automatic.backend().remove('?'), QString(backend).remove('?'));
        QVERIFY(!automatic.decode(unit).image.isNull());
        // 只记录调用耗时，测试机负载与重复 IDR 不构成真实 60 FPS 验收。
        for (auto* decoder : {&software, &automatic}) {
            QElapsedTimer elapsed;
            elapsed.start();
            for (int i = 0; i < 30; ++i) QVERIFY(!decoder->decode(unit).image.isNull());
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
        QVERIFY(!decoder.decode(file.readAll()).image.isNull());
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
        QVERIFY(!decoder.decode(file.readAll()).image.isNull());
    }
    void malformedAccessUnitDoesNotDisableHardware() {
        HevcDecoder decoder;
        const auto selected = decoder.backend();
        QVERIFY(decoder.decode("not a HEVC access unit").image.isNull());
        QCOMPARE(decoder.backend(), selected);
        decoder.reset();
        QFile file(PIP_VIDEO_HD_FIXTURE);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY2(!decoder.decode(file.readAll()).image.isNull(), qPrintable(decoder.error()));
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
