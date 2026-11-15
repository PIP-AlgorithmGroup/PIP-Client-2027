#pragma once

#include <QString>

/** 工作期间保持Windows后台窗口对精确定时器请求的响应，Ubuntu使用系统原有策略。
 * 仅改变Windows忽略定时精度请求的电源策略位，不改变CPU优先级、频率或定时周期。
 * 多实例共享进程策略，最后一份守卫析构时恢复原位；实例不可复制，构造/析构可跨线程。
 * 守卫须晚于所属精确定时器停止而析构；失败保留原策略，由error()返回原因。
 */
class BackgroundTimerResolution final {
   public:
    /** 尽力申请后台精确定时策略；不创建线程、周期定时器或永久系统设置。 */
    BackgroundTimerResolution();
    /** 最后一份有效守卫恢复原策略；恢复失败记录诊断，不抛异常。 */
    ~BackgroundTimerResolution();
    BackgroundTimerResolution(const BackgroundTimerResolution&) = delete;
    BackgroundTimerResolution& operator=(const BackgroundTimerResolution&) = delete;
    /** 返回构造失败的原生错误说明；成功或Ubuntu无需调整时为空，实例存活期内有效。 */
    const QString& error() const { return error_; }

   private:
    bool is_acquired_ = false;  ///< 实例独占；只在成功加入进程共享策略后置true。
    QString error_;
};
