// 独立、无 Qt 的更新交接进程。复制到缓存后运行，不能锁住待升级目录的 DLL/可执行文件。
// 仅接受调用者提供的已校验包：等待旧进程退出→调用系统安装器→记录结果→以原用户重启。
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

namespace {
namespace fs = std::filesystem;
using Path = fs::path;

#ifdef _WIN32
// CreateProcess 不接受 argv 数组；按 Windows CRT 规则转义尾部反斜杠和引号。
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
int launch(const Path& program, const std::vector<Path>& arguments, bool wait) {
    auto command = quote(program.native());
    for (const auto& argument : arguments) command += L" " + quote(argument.native());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        program.parent_path().c_str(), &startup, &process))
        return -1;
    CloseHandle(process.hThread);
    DWORD code = 0;
    if (wait) {
        WaitForSingleObject(process.hProcess, INFINITE);
        if (!GetExitCodeProcess(process.hProcess, &code)) code = DWORD(-1);
    }
    CloseHandle(process.hProcess);
    return int(code);
}
bool waitForParent(unsigned long pid) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!parent) return GetLastError() == ERROR_INVALID_PARAMETER;
    const auto result = WaitForSingleObject(parent, 60000);
    CloseHandle(parent);
    return result == WAIT_OBJECT_0;
}
#else
int launch(const Path& program, const std::vector<Path>& arguments, bool wait) {
    int errors[2];
    if (pipe2(errors, O_CLOEXEC) == -1) return -1;
    const auto pid = fork();
    if (pid < 0) {
        close(errors[0]);
        close(errors[1]);
        return -1;
    }
    if (pid == 0) {
        close(errors[0]);
        std::vector<std::string> values{program.string()};
        for (const auto& argument : arguments) values.push_back(argument.string());
        std::vector<char*> argv;
        for (auto& value : values) argv.push_back(value.data());
        argv.push_back(nullptr);
        execv(program.c_str(), argv.data());
        const auto error = errno;
        const auto ignored = write(errors[1], &error, sizeof(error));
        (void)ignored;
        _exit(127);
    }
    close(errors[1]);
    int error = 0;
    ssize_t read_size;
    do {
        read_size = read(errors[0], &error, sizeof(error));
    } while (read_size == -1 && errno == EINTR);
    close(errors[0]);
    if (read_size != 0) {
        int ignored;
        waitpid(pid, &ignored, 0);
        return -1;
    }
    if (!wait) return 0;
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
bool waitForParent(unsigned long pid) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
        if (kill(pid_t(pid), 0) == -1) return errno == ESRCH;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}
#endif

int perform(const std::vector<Path>& args) {
    if (args.size() != 5) return 2;
    unsigned long parent = 0;
    try {
        const auto value = args[1].string();
        size_t consumed = 0;
        parent = std::stoul(value, &consumed);
        if (consumed != value.size() || parent == 0 || parent > 0x7fffffffUL) return 2;
    } catch (...) {
        return 2;
    }
    const auto package = args[2], application = args[3], result_file = args[4];
    if (!package.is_absolute() || !application.is_absolute() || !result_file.is_absolute() ||
        !fs::is_regular_file(package) || !fs::is_regular_file(application))
        return 2;
    const auto record = [&](const char* value) {
        std::ofstream stream(result_file, std::ios::trunc);
        stream << value << '\n';
        return bool(stream);
    };
    // 超时绝不强杀用户程序，也不在旧版仍运行时调用安装器。
    if (!waitForParent(parent)) {
        record("parent-timeout");
        return 3;
    }
    int code;
#ifdef _WIN32
    code = launch(
        package,
        {L"/VERYSILENT", L"/SUPPRESSMSGBOXES", L"/NORESTART", L"/SP-", L"/NOCLOSEAPPLICATIONS",
         Path(L"/DIR=" + application.parent_path().parent_path().native())},
        true);
#elif defined(PIP_UPDATE_HELPER_TEST)
    // 测试目标编译时替换授权安装边界，生产目标永远调用固定的 pkexec/apt-get。
    code = launch(package, {}, true);
#else
    code = launch("/usr/bin/pkexec",
                  {"/usr/bin/apt-get", "--yes", "--no-remove", "install", package}, true);
#endif
    record(code == 0 ? "success" : "failed-or-cancelled");
    // 取消授权或安装失败也恢复原应用；不提权启动客户端，不自动清理用户配置。
    const auto restarted = launch(application, {}, false);
    if (restarted != 0) record("restart-failed");
    return restarted == 0 ? (code == 0 ? 0 : 4) : 5;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    try {
        std::vector<Path> arguments;
        for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
        return perform(arguments);
    } catch (...) {
        // 独立助手没有 GUI；异常返回非零，保留已下载包和旧安装供用户恢复。
        return 6;
    }
}
