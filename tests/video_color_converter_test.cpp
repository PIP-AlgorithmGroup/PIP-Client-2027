#include "video/video_color_converter.h"

#include <QTest>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace {
struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
Frame createFrame(AVPixelFormat format, int width, int height) {
    Frame frame(av_frame_alloc());
    if (!frame) return {};
    frame->format = format;
    frame->width = width;
    frame->height = height;
    if (av_frame_get_buffer(frame.get(), 32) < 0) return {};
    return frame;
}
}  // namespace

/** NV12 拆平面必须逐像素匹配独立的平面 YUV 参考，覆盖矩阵、范围与缓存规格变化。 */
class VideoColorConverterTest : public QObject {
    Q_OBJECT
   private slots:
    void layoutConversionPreservesColor_data() {
        QTest::addColumn<int>("matrix");
        QTest::addColumn<bool>("full_range");
        for (const int matrix : {AVCOL_SPC_BT470BG, AVCOL_SPC_BT709, AVCOL_SPC_BT2020_NCL})
            for (bool full : {false, true})
                QTest::newRow(qPrintable(QString("matrix%1-full%2").arg(matrix).arg(full)))
                    << matrix << full;
    }
    void layoutConversionPreservesColor() {
        QFETCH(int, matrix);
        QFETCH(bool, full_range);
        VideoColorConverter converter;
        QImage retained;
        QImage retained_copy;
        // 同一对象跨分辨率及 NV12/平面输入复用，避免上一规格缓存污染下一画面。
        for (const auto size : {QSize(320, 180), QSize(1920, 1080), QSize(128, 72)}) {
            auto planar = createFrame(AV_PIX_FMT_YUV420P, size.width(), size.height());
            auto nv12 = createFrame(AV_PIX_FMT_NV12, size.width(), size.height());
            QVERIFY(planar && nv12);
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x)
                    planar->data[0][y * planar->linesize[0] + x] =
                        nv12->data[0][y * nv12->linesize[0] + x] = (x * 7 + y * 3) % 256;
            for (int y = 0; y < size.height() / 2; ++y)
                for (int x = 0; x < size.width() / 2; ++x) {
                    const auto u = static_cast<uint8_t>((x * 13 + y * 11) % 256);
                    const auto v = static_cast<uint8_t>((x * 17 + y * 19) % 256);
                    planar->data[1][y * planar->linesize[1] + x] = u;
                    planar->data[2][y * planar->linesize[2] + x] = v;
                    nv12->data[1][y * nv12->linesize[1] + 2 * x] = u;
                    nv12->data[1][y * nv12->linesize[1] + 2 * x + 1] = v;
                }
            for (auto* frame : {planar.get(), nv12.get()}) {
                frame->colorspace = static_cast<AVColorSpace>(matrix);
                frame->color_range = full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
            }
            QImage reference(size, QImage::Format_RGBA8888);
            auto* sws = sws_getContext(size.width(), size.height(), AV_PIX_FMT_YUV420P,
                                       size.width(), size.height(), AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                       nullptr, nullptr, nullptr);
            QVERIFY(sws);
            const auto* coefficients =
                sws_getCoefficients(matrix == AVCOL_SPC_BT470BG ? SWS_CS_ITU601
                                    : matrix == AVCOL_SPC_BT709 ? SWS_CS_ITU709
                                                                : SWS_CS_BT2020);
            sws_setColorspaceDetails(sws, coefficients, full_range, coefficients, 1, 0, 1 << 16,
                                     1 << 16);
            uint8_t* planes[] = {reference.bits(), nullptr, nullptr, nullptr};
            int strides[] = {static_cast<int>(reference.bytesPerLine()), 0, 0, 0};
            const int rows =
                sws_scale(sws, planar->data, planar->linesize, 0, size.height(), planes, strides);
            sws_freeContext(sws);
            QCOMPARE(rows, size.height());
            QCOMPARE(converter.convert(nv12.get()), reference);
            QCOMPARE(converter.convert(planar.get()), reference);
            if (!retained.isNull()) QCOMPARE(retained, retained_copy);
            retained = converter.convert(nv12.get());
            retained_copy = retained.copy();
        }
    }
    void invalidInputCanRecover() {
        VideoColorConverter converter;
        QVERIFY(converter.convert(nullptr).isNull());
        QVERIFY(!converter.error().isEmpty());
        auto frame = createFrame(AV_PIX_FMT_NV12, 64, 32);
        QVERIFY(frame);
        memset(frame->data[0], 16, frame->linesize[0] * frame->height);
        memset(frame->data[1], 128, frame->linesize[1] * frame->height / 2);
        frame->color_range = AVCOL_RANGE_MPEG;
        const auto image = converter.convert(frame.get());
        QVERIFY(!image.isNull());
        QVERIFY(converter.error().isEmpty());
        QCOMPARE(image.pixelColor(4, 4), QColor(0, 0, 0, 255));
    }
};
QTEST_GUILESS_MAIN(VideoColorConverterTest)
#include "video_color_converter_test.moc"
