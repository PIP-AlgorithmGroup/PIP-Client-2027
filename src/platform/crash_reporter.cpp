#include "crash_reporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// DbgHelp 的 MinGW 头依赖 Windows 类型，必须放在 windows.h 之后的独立 include 组。
#include <dbghelp.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
// 致命路径只操作栈上固定缓冲，不调用格式化库、Qt、锁或堆分配。
struct EmergencyLine {
    char bytes[256]{};
    unsigned int size = 0;

    void append(const char* value) {
        while (*value && size < sizeof(bytes)) bytes[size++] = *value++;
    }
    void number(std::uintptr_t value, unsigned int base = 10) {
        char reversed[2 * sizeof(value) + 20]{};
        unsigned int digits = 0;
        do {
            reversed[digits++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value && digits < sizeof(reversed));
        while (digits && size < sizeof(bytes)) bytes[size++] = reversed[--digits];
    }
};
}  // namespace

struct CrashReporter::Impl {
    // 仅安装/卸载线程改变所有权。安装后致命回调只访问原生句柄，不访问外层 Qt 对象。
    static std::atomic<Impl*> active;
    static_assert(std::atomic<Impl*>::is_always_lock_free);
    QString marker_path;
    bool is_installed = false;
    std::terminate_handler previous_terminate = nullptr;
#ifdef Q_OS_WIN
    using DumpWriter = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                     PMINIDUMP_EXCEPTION_INFORMATION,
                                     PMINIDUMP_USER_STREAM_INFORMATION,
                                     PMINIDUMP_CALLBACK_INFORMATION);
    HANDLE marker = INVALID_HANDLE_VALUE;
    HANDLE dump = INVALID_HANDLE_VALUE;
    HMODULE dbghelp = nullptr;
    DumpWriter write_dump = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
    using SignalHandler = void (*)(int);
    SignalHandler previous_abort = SIG_DFL;
    QString dump_path;
    volatile LONG is_handling = 0;

    static LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* exception) {
        auto* state = active.load(std::memory_order_acquire);
        if (!state || InterlockedExchange(&state->is_handling, 1)) return EXCEPTION_EXECUTE_HANDLER;
        state->capture("unhandled_exception", exception);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    static void abortHandler(int) {
        auto* state = active.load(std::memory_order_acquire);
        if (state && !InterlockedExchange(&state->is_handling, 1))
            state->captureCurrent("abort", 0xe0504950);
        // 不重入 CRT abort 或弹出错误窗口；证据落盘后立即终止，不运行损坏进程的析构。
        TerminateProcess(GetCurrentProcess(), 134);
        std::_Exit(134);
    }
    static void terminateHandler() {
        auto* state = active.load(std::memory_order_acquire);
        if (state && !InterlockedExchange(&state->is_handling, 1))
            state->captureCurrent("terminate", 0xe0504951);
        TerminateProcess(GetCurrentProcess(), 134);
        std::_Exit(134);
    }
    void captureCurrent(const char* reason, DWORD code) {
        CONTEXT context{};
        RtlCaptureContext(&context);
        EXCEPTION_RECORD record{};
        record.ExceptionCode = code;
#if defined(_M_X64) || defined(__x86_64__)
        record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
        record.ExceptionAddress = reinterpret_cast<void*>(context.Eip);
#elif defined(_M_ARM64) || defined(__aarch64__)
        record.ExceptionAddress = reinterpret_cast<void*>(context.Pc);
#endif
        EXCEPTION_POINTERS exception{&record, &context};
        capture(reason, &exception);
    }
    void capture(const char* reason, EXCEPTION_POINTERS* exception) {
        EmergencyLine line;
        line.append("reason=");
        line.append(reason);
        line.append(" pid=");
        line.number(GetCurrentProcessId());
        line.append(" thread=");
        line.number(GetCurrentThreadId());
        if (exception && exception->ExceptionRecord) {
            line.append(" exception=0x");
            line.number(exception->ExceptionRecord->ExceptionCode, 16);
            line.append(" address=0x");
            line.number(
                reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress), 16);
        }
        line.append("\n");
        DWORD written = 0;
        WriteFile(marker, line.bytes, line.size, &written, nullptr);
        FlushFileBuffers(marker);
        // 进程内最小转储仍为 best effort；损坏堆、loader lock 或并发 DbgHelp 调用可使其失败。
        MINIDUMP_EXCEPTION_INFORMATION information{GetCurrentThreadId(), exception, FALSE};
        const BOOL has_dump = write_dump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                                         MiniDumpNormal, &information, nullptr, nullptr);
        const DWORD dump_error = has_dump ? ERROR_SUCCESS : GetLastError();
        const char* result = has_dump ? "minidump=written\n" : "minidump=failed\n";
        EmergencyLine status;
        status.append(result);
        if (!has_dump) {
            status.append("minidump_error=");
            status.number(dump_error);
            status.append("\n");
        }
        WriteFile(marker, status.bytes, status.size, &written, nullptr);
        FlushFileBuffers(marker);
        FlushFileBuffers(dump);
    }
#else
    static constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS};
    struct sigaction previous_actions[5]{};
    unsigned int installed_actions = 0;
    int marker = -1;

    static void signalHandler(int signal_number, siginfo_t* information, void*) {
        auto* state = active.load(std::memory_order_acquire);
        if (state) {
            EmergencyLine line;
            line.append("fatal_signal=");
            line.number(static_cast<unsigned int>(signal_number));
            line.append(" pid=");
            line.number(static_cast<unsigned int>(getpid()));
            if (information) {
                line.append(" code=");
                // si_code 可能为负；用绝对值和显式符号记录，避免无符号转换成巨大的编号。
                if (information->si_code < 0) line.append("-");
                line.number(static_cast<unsigned int>(
                    information->si_code < 0 ? -information->si_code : information->si_code));
                if (information->si_code > 0 && signal_number != SIGABRT) {
                    // 用户发送信号和 abort 的 siginfo union 不含有效故障地址。
                    line.append(" address=0x");
                    line.number(reinterpret_cast<std::uintptr_t>(information->si_addr), 16);
                }
            }
            line.append("\n");
            // POSIX write 可异步信号调用；短写或磁盘故障不触发递归日志，也不申请内存。
            const auto ignored = ::write(state->marker, line.bytes, line.size);
            (void)ignored;
        }
        struct sigaction action{};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(signal_number, &action, nullptr);
        // 同线程重新发送，处理器返回时由默认动作退出并按系统 core 配置保留现场。
        // 不修改 core 大小、core_pattern 或系统崩溃服务，也不强行生成不完整的文本调用栈。
        raise(signal_number);
    }
    static void terminateHandler() {
        auto* state = active.load(std::memory_order_acquire);
        if (state) {
            constexpr char reason[] = "reason=terminate\n";
            const auto ignored = ::write(state->marker, reason, sizeof(reason) - 1);
            (void)ignored;
        }
        std::abort();
    }
#endif

    ~Impl() { uninstall(); }
    void uninstall() {
        if (is_installed) {
            std::set_terminate(previous_terminate);
#ifdef Q_OS_WIN
            SetUnhandledExceptionFilter(previous_filter);
            std::signal(SIGABRT, previous_abort);
#else
            for (unsigned int index = 0; index < installed_actions; ++index)
                sigaction(kSignals[index], &previous_actions[index], nullptr);
#endif
            active = nullptr;
            is_installed = false;
        }
#ifdef Q_OS_WIN
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
        if (dump != INVALID_HANDLE_VALUE) CloseHandle(dump);
        marker = dump = INVALID_HANDLE_VALUE;
        if (dbghelp) FreeLibrary(dbghelp);
        dbghelp = nullptr;
        if (!dump_path.isEmpty() && QFileInfo(dump_path).size() == 0) QFile::remove(dump_path);
        dump_path.clear();
#else
        if (marker >= 0) ::close(marker);
        marker = -1;
        installed_actions = 0;
#endif
        if (!marker_path.isEmpty() && QFileInfo(marker_path).size() == 0)
            QFile::remove(marker_path);
        marker_path.clear();
    }
};

std::atomic<CrashReporter::Impl*> CrashReporter::Impl::active{nullptr};

CrashReporter::CrashReporter() : impl_(std::make_unique<Impl>()) {}
CrashReporter::~CrashReporter() = default;

bool CrashReporter::install(const QString& session_directory, QString* error) {
    const auto fail = [&](const QString& message) {
        impl_->uninstall();
        if (error) *error = message;
        return false;
    };
    if (impl_->is_installed || Impl::active) {
        if (error) *error = "崩溃证据处理器已经安装。";
        return false;
    }
    if (session_directory.isEmpty() || !QFileInfo(session_directory).isDir())
        return fail("崩溃证据目录不存在。");
    const auto marker_path = QDir(session_directory).absoluteFilePath("crash.txt");
#ifdef Q_OS_WIN
    // 从系统目录加载，避免受当前目录或 PATH 中同名 DLL 影响；崩溃时不再执行加载器查找。
    wchar_t system_directory[MAX_PATH + 1]{};
    const auto length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return fail("无法定位系统 DbgHelp。");
    const auto library_path = QString::fromWCharArray(system_directory) + "/dbghelp.dll";
    impl_->dbghelp = LoadLibraryW(reinterpret_cast<LPCWSTR>(library_path.utf16()));
    if (!impl_->dbghelp) return fail("无法加载系统 DbgHelp。");
    impl_->write_dump =
        reinterpret_cast<Impl::DumpWriter>(GetProcAddress(impl_->dbghelp, "MiniDumpWriteDump"));
    if (!impl_->write_dump) return fail("系统 DbgHelp 不提供最小转储接口。");
    impl_->marker =
        CreateFileW(reinterpret_cast<LPCWSTR>(marker_path.utf16()), GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl_->marker == INVALID_HANDLE_VALUE)
        return fail(QString("无法创建崩溃记录，系统错误 %1。").arg(GetLastError()));
    impl_->marker_path = marker_path;
    const auto dump_path = QDir(session_directory).absoluteFilePath("crash.dmp");
    impl_->dump = CreateFileW(reinterpret_cast<LPCWSTR>(dump_path.utf16()), GENERIC_WRITE,
                              FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl_->dump == INVALID_HANDLE_VALUE)
        return fail(QString("无法创建最小转储文件，系统错误 %1。").arg(GetLastError()));
    impl_->dump_path = dump_path;
    impl_->previous_abort = std::signal(SIGABRT, &Impl::abortHandler);
    if (impl_->previous_abort == SIG_ERR) return fail("无法安装中止处理器。");
    Impl::active = impl_.get();
    impl_->previous_filter = SetUnhandledExceptionFilter(&Impl::exceptionFilter);
#else
    const auto native_path = QFile::encodeName(marker_path);
    impl_->marker = ::open(native_path.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (impl_->marker < 0) return fail(QString("无法创建崩溃记录，系统错误 %1。").arg(errno));
    impl_->marker_path = marker_path;
    Impl::active = impl_.get();
    for (unsigned int index = 0; index < 5; ++index) {
        struct sigaction action{};
        action.sa_sigaction = &Impl::signalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        if (sigaction(Impl::kSignals[index], &action, &impl_->previous_actions[index]) != 0) {
            // 部分安装失败也恢复已替换项，不遗留指向待销毁对象的全局回调。
            const auto code = errno;
            for (unsigned int previous = 0; previous < impl_->installed_actions; ++previous)
                sigaction(Impl::kSignals[previous], &impl_->previous_actions[previous], nullptr);
            Impl::active = nullptr;
            return fail(QString("无法安装致命信号处理器，系统错误 %1。").arg(code));
        }
        ++impl_->installed_actions;
    }
#endif
    impl_->previous_terminate = std::set_terminate(&Impl::terminateHandler);
    impl_->is_installed = true;
    if (error) error->clear();
    return true;
}
