#include "background_timer_resolution.h"

#include <QDebug>
#include <mutex>

#ifdef Q_OS_WIN
#include <qt_windows.h>

namespace {
// 只保护本模块对进程策略的引用；不在持锁期间调用业务或Qt事件循环。
struct SharedPolicy {
    std::mutex mutex;
    unsigned users = 0;
    ULONG original_control = 0;
    ULONG original_state = 0;
};
SharedPolicy& sharedPolicy() {
    static SharedPolicy policy;
    return policy;
}
}  // namespace
#endif

BackgroundTimerResolution::BackgroundTimerResolution() {
#ifdef Q_OS_WIN
    auto& shared = sharedPolicy();
    const std::lock_guard lock(shared.mutex);
    if (shared.users != 0) {
        ++shared.users;
        is_acquired_ = true;
        return;
    }
    PROCESS_POWER_THROTTLING_STATE policy{};
    policy.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (!GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &policy,
                               sizeof(policy))) {
        error_ = QString("无法读取后台计时策略，Windows错误%1").arg(GetLastError());
        return;
    }
    shared.original_control = policy.ControlMask;
    shared.original_state = policy.StateMask;
    // Windows 11可在窗口完全遮挡/最小化后忽略Qt的timeBeginPeriod请求。
    // 显式关闭此忽略策略，仍由Qt按活跃PreciseTimer的需要申请和释放定时精度。
    policy.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    policy.StateMask &= ~PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &policy,
                               sizeof(policy))) {
        error_ = QString("无法设置后台计时策略，Windows错误%1").arg(GetLastError());
        return;
    }
    shared.users = 1;
    is_acquired_ = true;
#endif
}

BackgroundTimerResolution::~BackgroundTimerResolution() {
#ifdef Q_OS_WIN
    if (!is_acquired_) return;
    auto& shared = sharedPolicy();
    const std::lock_guard lock(shared.mutex);
    if (--shared.users != 0) return;
    PROCESS_POWER_THROTTLING_STATE policy{};
    policy.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (!GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &policy,
                               sizeof(policy))) {
        qWarning() << "后台计时策略恢复读取失败，Windows错误" << GetLastError();
        return;
    }
    // 只恢复本模块拥有的位；保留运行期间其他模块对CPU电源策略的修改。
    constexpr auto flag = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    policy.ControlMask = (policy.ControlMask & ~flag) | (shared.original_control & flag);
    policy.StateMask = (policy.StateMask & ~flag) | (shared.original_state & flag);
    if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &policy,
                               sizeof(policy)))
        qWarning() << "后台计时策略恢复失败，Windows错误" << GetLastError();
#endif
}
