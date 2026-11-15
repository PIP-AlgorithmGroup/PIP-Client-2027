#pragma once

#include <QObject>
#include <chrono>
#include <memory>

/** 通信线程独占的一次性高分辨率计时器；不创建线程、不自旋、不更改系统时钟。
 * Windows使用高分辨率waitable timer，Ubuntu使用Qt单调计时器；事件在拥有线程触发。
 * 拥有者须在销毁网络资源前stop；原生申请/设定失败通过start返回false，不静默降精度。
 */
class DeadlineTimer final : public QObject {
    Q_OBJECT
   public:
    /// 在当前事件线程构造，子资源随本对象释放。
    explicit DeadlineTimer(QObject* parent = nullptr);
    /// 取消未到期事件并释放原生句柄。
    ~DeadlineTimer() override;
    /// 重置一次性相对等待；负数按零处理。失败返回false，调用者应停止当前通信会话。
    bool start(std::chrono::microseconds delay);
    /// 取消当前等待；已经进入调用者的timeout回调仍由调用者的会话状态校验。
    void stop();
   signals:
    /// 在拥有线程发出一次；不表示协议截止时间已到，调用者仍须重查单调时钟。
    void timeout();

   private:
    struct State;
    std::unique_ptr<State> state_;
};
