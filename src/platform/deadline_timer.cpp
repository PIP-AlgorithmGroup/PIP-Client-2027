#include "platform/deadline_timer.h"

#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <QWinEventNotifier>
#else
#include <QChronoTimer>
#endif

struct DeadlineTimer::State {
#ifdef Q_OS_WIN
    HANDLE handle = nullptr;
    QWinEventNotifier* notifier = nullptr;
#else
    QChronoTimer* timer = nullptr;
#endif
};

DeadlineTimer::DeadlineTimer(QObject* parent) : QObject(parent), state_(std::make_unique<State>()) {
#ifdef Q_OS_WIN
    state_->handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                            TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!state_->handle) return;
    state_->notifier = new QWinEventNotifier(state_->handle, this);
    state_->notifier->setEnabled(false);
    connect(state_->notifier, &QWinEventNotifier::activated, this, [this] {
        state_->notifier->setEnabled(false);  // 一次性：先撤销事件源，回调可安全重设下一次等待。
        emit timeout();
    });
#else
    state_->timer = new QChronoTimer(this);
    state_->timer->setSingleShot(true);
    state_->timer->setTimerType(Qt::PreciseTimer);
    connect(state_->timer, &QChronoTimer::timeout, this, &DeadlineTimer::timeout);
#endif
}
DeadlineTimer::~DeadlineTimer() {
    stop();
#ifdef Q_OS_WIN
    delete state_->notifier;  // 先解除事件注册，再关闭句柄。
    if (state_->handle) CloseHandle(state_->handle);
#endif
}
bool DeadlineTimer::start(std::chrono::microseconds delay) {
    stop();
#ifdef Q_OS_WIN
    if (!state_->handle) return false;
    LARGE_INTEGER due;
    due.QuadPart = -std::max<qint64>(1, delay.count() * 10);  // 相对100ns；零不得解释为绝对时间。
    if (!SetWaitableTimer(state_->handle, &due, 0, nullptr, nullptr, FALSE)) return false;
    state_->notifier->setEnabled(true);
#else
    state_->timer->setInterval(std::max(delay, std::chrono::microseconds::zero()));
    state_->timer->start();
#endif
    return true;
}
void DeadlineTimer::stop() {
#ifdef Q_OS_WIN
    if (state_->notifier) state_->notifier->setEnabled(false);
    if (state_->handle) CancelWaitableTimer(state_->handle);
#else
    state_->timer->stop();
#endif
}
