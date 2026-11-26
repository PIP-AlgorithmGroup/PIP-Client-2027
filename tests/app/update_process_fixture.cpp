#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QThread>

/** 仅测试安装器/重启进程与旧进程退出交接，不修改系统包或用户安装。 */
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.value(1) == "--parent" && arguments.size() == 6) {
        const bool started = QProcess::startDetached(
            arguments[2],
            {QString::number(app.applicationPid()), arguments[3], arguments[4], arguments[5]});
        // 给测试留出确认“旧进程仍运行时不能安装”的窗口，而不是靠启动速度猜测。
        QThread::msleep(600);
        return started ? 0 : 1;
    }
    QFile marker(app.applicationFilePath() + ".ran");
    if (!marker.open(QIODevice::WriteOnly)) return 10;
    marker.write("ran\n");
    marker.close();
    QFile result(app.applicationFilePath() + ".exit");
    return result.open(QIODevice::ReadOnly) ? result.readAll().trimmed().toInt() : 0;
}
