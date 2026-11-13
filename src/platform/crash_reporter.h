#pragma once

#include <QString>
#include <memory>

/**
 * 本次启动的原生崩溃证据守卫；不上传数据，不尝试恢复损坏进程。
 * 启动线程在创建业务工作线程前安装，业务线程退出后析构；同进程只允许一个实例安装。
 * Windows 尽力写入最小转储，Linux 留下信号记录并保留系统默认 core 行为。
 * OOM、栈耗尽、fast-fail、强杀或损坏的操作系统/驱动状态下不保证能产生完整证据。
 * Windows 的 Qt 致命日志须由文件日志处理器落盘后调用 std::abort；Qt 自身 fail-fast 会绕过 SEH。
 */
class CrashReporter {
   public:
    /** 创建未安装的守卫；构造不改动进程级处理器。 */
    CrashReporter();
    /** 恢复原进程处理器并关闭句柄；正常运行时删除空的崩溃占位文件。 */
    ~CrashReporter();
    CrashReporter(const CrashReporter&) = delete;
    CrashReporter& operator=(const CrashReporter&) = delete;

    /**
     * 在已有的本次会话目录中预开 crash.txt；Windows 另预开 crash.dmp 并加载系统 DbgHelp。
     * 路径为空、文件已存在、原生资源失败或已有守卫时返回 false 并填写 error（非空时）。
     * 成功后重复调用返回 false；安装与析构不能并发，也不能与其他崩溃处理器管理代码混用。
     * 平时不执行磁盘写入；紧急记录路径不调用 Qt、堆分配或应用锁。
     * Windows DbgHelp 的转储内部行为不受本模块控制，故转储只保证尽力而为。
     */
    bool install(const QString& session_directory, QString* error = nullptr);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
