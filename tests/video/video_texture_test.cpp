#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

#include "video/hevc_decoder.h"
#include "video/presentation_window.h"
#include "video/video_texture_node.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace {
// GUI 设置帧、同步阶段读取；path 仅在 grabWindow 已等待渲染后读取，避免跨线程轮询。
class TestVideoItem : public QQuickItem {
   public:
    VideoFrame frame;
    QString path;
    QString fallback;
    explicit TestVideoItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override {
        auto* node = static_cast<VideoTextureNode*>(old);
        if (frame.isNull()) {
            delete node;
            return nullptr;
        }
        if (!node) node = new VideoTextureNode;
        if (!node->setFrame(frame, window())) {
            delete node;
            return nullptr;
        }
        node->setRect(boundingRect());
        path = node->path();
        fallback = node->fallbackReason();
        return node;
    }
};

VideoFrame colorFrame(AVPixelFormat format, int matrix, bool full, QSize size, int offset,
                      bool negative_stride) {
    auto* pixels = av_frame_alloc();
    if (!pixels) return {};
    pixels->format = format;
    pixels->width = size.width();
    pixels->height = size.height();
    pixels->colorspace = AVColorSpace(matrix);
    pixels->color_range = full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    pixels->chroma_location = AVCHROMA_LOC_LEFT;
    if (av_frame_get_buffer(pixels, 32) < 0) {
        av_frame_free(&pixels);
        return {};
    }
    const bool ten = format == AV_PIX_FMT_YUV420P10LE || format == AV_PIX_FMT_P010LE;
    const bool semi = format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010LE;
    for (int p = 0; p < (semi ? 2 : 3); ++p) {
        const int width = p == 0 ? size.width() : (size.width() + 1) / 2;
        const int height = p == 0 ? size.height() : (size.height() + 1) / 2;
        for (int y = 0; y < height; ++y) {
            auto* row = pixels->data[p] + y * pixels->linesize[p];
            for (int x = 0; x < width; ++x) {
                // 四象限平色验证矩阵、范围、通道与翻转；边缘另由采样偏移处理。
                const int luma =
                    32 + offset + (x >= width / 2 ? 80 : 0) + (y >= height / 2 ? 40 : 0);
                const int u = 80 + (x >= width / 2 ? 70 : 0);
                const int v = 90 + (y >= height / 2 ? 80 : 0);
                const auto store = [&](int index, int value) {
                    if (ten)
                        reinterpret_cast<quint16*>(row)[index] =
                            quint16(value * 4 * (format == AV_PIX_FMT_P010LE ? 64 : 1));
                    else
                        row[index] = quint8(value);
                };
                if (p == 0)
                    store(x, luma);
                else if (semi) {
                    store(2 * x, u);
                    store(2 * x + 1, v);
                } else
                    store(x, p == 1 ? u : v);
            }
        }
        if (negative_stride) {
            pixels->data[p] += (height - 1) * pixels->linesize[p];
            pixels->linesize[p] = -pixels->linesize[p];
        }
    }
    VideoFrame result;
    result.pixels = std::shared_ptr<const AVFrame>(pixels, [](const AVFrame* frame) {
        auto* owned = const_cast<AVFrame*>(frame);
        av_frame_free(&owned);
    });
    return result;
}
}  // namespace

/** 运行真实 Qt 场景图并与 FFmpeg CPU 参考比较；software 与 GPU 实测结果分别记录。 */
class VideoTextureTest : public QObject {
    Q_OBJECT
   private slots:
    void colorsAndTextureReuse_data() {
        QTest::addColumn<int>("format");
        QTest::addColumn<int>("matrix");
        QTest::addColumn<bool>("full");
        for (int format :
             {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_P010LE})
            for (int matrix : {AVCOL_SPC_BT470BG, AVCOL_SPC_BT709, AVCOL_SPC_BT2020_NCL})
                for (bool full : {false, true})
                    QTest::newRow(qPrintable(
                        QString("fmt%1-matrix%2-full%3").arg(format).arg(matrix).arg(full)))
                        << format << matrix << full;
    }
    void colorsAndTextureReuse() {
        QFETCH(int, format);
        QFETCH(int, matrix);
        QFETCH(bool, full);
        PresentationWindow window;
        window.setColor(Qt::black);
        window.resize(360, 220);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* item = new TestVideoItem(window.contentItem());
        // 同规格改像素、切换规格及负 stride，验证持久纹理不会显示上一帧/上一尺寸。
        for (int revision = 0; revision < 3; ++revision) {
            const QSize size = revision == 2 ? QSize(321, 181) : QSize(320, 180);
            item->setSize(size);
            item->frame =
                colorFrame(AVPixelFormat(format), matrix, full, size, revision * 12, revision == 2);
            QVERIFY(!item->frame.isNull());
            item->update();
            const auto screenshot = window.grabFrame();
            QVERIFY(!screenshot.isNull());
            const auto expected = item->frame.toImage();
            QVERIFY(!expected.isNull());
            const qreal scale = screenshot.width() / qreal(window.width());
            for (const auto fraction :
                 {QPointF(0.2, 0.2), QPointF(0.8, 0.2), QPointF(0.2, 0.8), QPointF(0.8, 0.8)}) {
                const QPoint point(int(size.width() * fraction.x()),
                                   int(size.height() * fraction.y()));
                const QColor observed =
                    screenshot.pixelColor(QPoint(int(point.x() * scale), int(point.y() * scale)));
                const QColor reference = expected.pixelColor(point);
                const int difference = qMax(qAbs(observed.red() - reference.red()),
                                            qMax(qAbs(observed.green() - reference.green()),
                                                 qAbs(observed.blue() - reference.blue())));
                QVERIFY2(difference <= 6,
                         qPrintable(QString("%1 observed=%2 expected=%3 diff=%4")
                                        .arg(item->path, observed.name(), reference.name())
                                        .arg(difference)));
            }
            if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software ||
                qEnvironmentVariable("PIP_VIDEO_PRESENTATION") == "rgba")
                QCOMPARE(item->path, QString("rgba-upload"));
            else
                QCOMPARE(item->path, QString("yuv-upload"));
        }
        item->frame = {};
        item->update();
        QVERIFY(!window.grabFrame().isNull());
        window.hide();
        window.releaseResources();
    }
    void hardwareImportAndFallback_data() {
        QTest::addColumn<QString>("fixture");
        QTest::newRow("nv12") << QString(PIP_VIDEO_HD_FIXTURE);
        QTest::newRow("p010") << QString(PIP_VIDEO_HD10_FIXTURE);
    }
    void hardwareImportAndFallback() {
        QFETCH(QString, fixture);
        QFile file(fixture);
        QVERIFY(file.open(QIODevice::ReadOnly));
        HevcDecoder decoder;
        const auto unit = file.readAll();
        auto frame = decoder.decode(unit);
        if (!decoder.hardwareActive()) QSKIP(qPrintable(decoder.fallbackReason()));
        QVERIFY(!frame.isNull());
        const auto expected = frame.toImage();
        QVERIFY(!expected.isNull());
        PresentationWindow window;
        window.resize(480, 270);
        auto* item = new TestVideoItem(window.contentItem());
        item->setSize({480, 270});
        item->frame = frame;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int i = 0; i < 12; ++i) {
            item->frame = decoder.decode(unit);
            QVERIFY(!item->frame.isNull());
            item->update();
            const auto screenshot = window.grabFrame().scaled(expected.size());
            QVERIFY(!screenshot.isNull());
            for (const auto fraction :
                 {QPointF(0.2, 0.2), QPointF(0.8, 0.2), QPointF(0.2, 0.8), QPointF(0.8, 0.8)}) {
                const QPoint point(int(expected.width() * fraction.x()),
                                   int(expected.height() * fraction.y()));
                const auto observed = screenshot.pixelColor(point);
                const auto reference = expected.pixelColor(point);
                QVERIFY(qAbs(observed.red() - reference.red()) <= 8);
                QVERIFY(qAbs(observed.green() - reference.green()) <= 8);
                QVERIFY(qAbs(observed.blue() - reference.blue()) <= 8);
            }
        }
        qInfo().noquote() << "HARDWARE_PRESENTATION" << item->path << item->fallback;
        const auto required_path = qEnvironmentVariable("PIP_VIDEO_TEST_REQUIRED_PATH");
        if (!required_path.isEmpty()) QCOMPARE(item->path, required_path);
        if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
            QCOMPARE(item->path, QString("rgba-upload"));
            if (qEnvironmentVariable("PIP_VIDEO_PRESENTATION") != "rgba")
                QVERIFY(!item->fallback.isEmpty());
        }
        // 硬件帧持有跨 decoder reset；窗口释放后重建场景图也必须显示原始有效 surface。
        decoder.reset();
        window.setPersistentSceneGraph(false);
        window.setPersistentGraphics(false);
        window.hide();
        window.releaseResources();
        window.destroy();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        item->update();
        QTRY_VERIFY(!window.grabFrame().isNull());
        if (!required_path.isEmpty()) QCOMPARE(item->path, required_path);
    }
};
QTEST_MAIN(VideoTextureTest)
#include "video_texture_test.moc"
