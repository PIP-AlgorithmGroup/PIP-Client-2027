#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <cstdio>

/**
 * Render one static 512-unit SVG to an RGBA PNG using four samples per output axis.
 * The renderer and all image memory stay on this tool's main thread. A failed write reports the
 * affected path and returns false; no GUI, display server or platform plugin is required.
 */
static bool renderIcon(QSvgRenderer& renderer, const QDir& output, int size) {
    constexpr int kSupersampling = 4;
    QImage image(size * kSupersampling, size * kSupersampling, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();
    image = image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const auto output_path = output.filePath(QString("pip_client_%1.png").arg(size));
    if (!image.save(output_path, "PNG")) {
        QTextStream(stderr) << "Cannot write icon PNG: " << output_path << Qt::endl;
        return false;
    }
    return true;
}

/**
 * Author icons with: pip_icon_renderer STANDARD.svg OUTPUT_DIRECTORY SMALL_16.svg.
 * Both inputs must be static SVGs with viewBox 0 0 512 512. The small source is used only at 16px;
 * larger sizes use the standard source. Returns nonzero on invalid input or an output failure.
 * Sources contain no text, so QCoreApplication suffices for the CPU-only QImage painting path.
 */
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 4) {
        QTextStream(stderr) << "Usage: pip_icon_renderer STANDARD.svg OUTPUT_DIRECTORY SMALL_16.svg"
                            << Qt::endl;
        return 1;
    }
    QSvgRenderer standard(arguments[1]);
    QSvgRenderer small(arguments[3]);
    const QRectF expected_view_box(0, 0, 512, 512);
    for (const auto* renderer : {&standard, &small}) {
        if (!renderer->isValid() || renderer->animated() ||
            renderer->viewBoxF() != expected_view_box) {
            QTextStream(stderr)
                << "Both icon sources must be valid static SVGs with viewBox 0 0 512 512"
                << Qt::endl;
            return 2;
        }
    }
    const QDir output(arguments[2]);
    if (!output.mkpath(".")) {
        QTextStream(stderr) << "Cannot create icon output directory: " << output.absolutePath()
                            << Qt::endl;
        return 3;
    }
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
        if (!renderIcon(size == 16 ? small : standard, output, size)) return 4;
    }
    return 0;
}
