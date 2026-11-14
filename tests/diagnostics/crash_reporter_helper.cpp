#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <thread>

#include "diagnostics/session_log.h"
#include "platform/crash_reporter.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/** 子进程专用故障注入；不会链接到产品，也不接受产品命令行中的崩溃开关。 */
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    const auto directory = app.arguments().at(1);
    const auto mode = app.arguments().at(2);
    QString error;
    if (mode == "fatal") {
        SessionLog::Options options;
        options.root_directory = directory;
        SessionLog log(options);
        if (!log.error().isEmpty() || !log.installQtHandler()) return 7;
        CrashReporter reporter;
        if (!reporter.install(log.sessionDirectory(), &error)) return 8;
        // 使用产品完整 Qt 日志→落盘→abort→原生崩溃证据路径，而不是复制一份测试 handler。
        qFatal("crash reporter helper fatal message");
    }
    {
        CrashReporter reporter;
        if (!reporter.install(directory, &error)) return 3;
        if (mode == "abort") std::abort();
        if (mode == "terminate") std::terminate();
        if (mode == "worker-abort") std::thread([] { std::abort(); }).join();
        if (mode == "worker-terminate") std::thread([] { std::terminate(); }).join();
        if (mode == "worker-access") {
            std::thread([] {
                auto* address = reinterpret_cast<volatile int*>(std::uintptr_t(0));
                *address = 7;
            }).join();
        }
        if (mode == "access") {
            // volatile 零地址写入产生原生访问违规；测试只在隔离子进程中执行。
            auto* address = reinterpret_cast<volatile int*>(std::uintptr_t(0));
            *address = 7;
            return 4;
        }
        if (mode == "duplicate") {
            CrashReporter duplicate;
            if (duplicate.install(directory, &error) || error.isEmpty()) return 5;
        }
    }
    if (mode == "restore") {
        CrashReporter replacement;
        if (!replacement.install(directory, &error)) return 6;
    }
    return 0;
}
