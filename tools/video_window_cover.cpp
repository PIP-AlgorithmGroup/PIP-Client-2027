#include <QGuiApplication>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

/** 性能实验的独立遮挡进程；只创建测试窗口，不修改系统或其他应用配置。
 * 四个参数为逻辑像素 x/y/width/height；由父测试通过进程生命周期回收。
 * 必须是独立进程，避免同进程可见窗口让 Windows 将被测进程继续视为前台可见。
 */
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 5) return 2;
    int values[4]{};
    for (int i = 0; i < 4; ++i) {
        bool is_valid = false;
        values[i] = args[i + 1].toInt(&is_valid);
        if (!is_valid) return 2;
    }
    if (values[2] <= 0 || values[3] <= 0) return 2;
    QQuickWindow window;
    window.setFlag(Qt::WindowStaysOnTopHint);
    window.setTitle(QStringLiteral("PIP presentation probe cover"));
    window.setColor(QColor("#30343a"));
    window.setGeometry(values[0], values[1], values[2], values[3]);
    window.show();
    window.requestActivate();
    // 给首次平台窗口创建留一次事件循环，再请求激活；不全局修改前台锁策略。
    QTimer::singleShot(100, &window, &QWindow::requestActivate);
    QTimer::singleShot(300, &window, &QWindow::requestActivate);
    QTimer::singleShot(500, &window, [&] {
        QTextStream(stdout) << "COVER_READY " << QGuiApplication::platformName() << ' '
                            << window.isExposed() << ' ' << window.isActive() << Qt::endl;
    });
    return app.exec();
}
