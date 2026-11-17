#pragma once

#include <algorithm>
#include <cstdint>

/** MQTT 线程独占的键鼠节拍；时间为同一 steady_clock 的绝对微秒数。
 * 仅管理发送资格，不保存输入或消息；每次到期仍需从 InputState 读取最新快照。
 * 200Hz/75Hz 名义周期为 5000/13334us，最短间隔为 3000/10000us。
 * 连续发送最多保留四周期相位预算；长于四周期没有交付则重定相，不积攒无限发送额度。
 */
class InputCadence {
   public:
    /** 切换已验证的 75/200Hz 配置；保留上次交付时间，防止配置切换绕过最短间隔。 */
    void setFrequency(int frequency) {
        if (frequency_ == frequency) return;
        frequency_ = frequency;
        phase_us_ = 0;
    }
    /** 没有有效控制/连接时丢弃相位预算；保留最后交付时间，恢复时仍受最短间隔约束。 */
    void suspend() { phase_us_ = 0; }
    /** 返回下一次可发送的绝对时间；尚未交付任何帧时可立即发送。 */
    std::int64_t readyAt() const {
        if (!last_us_) return 0;
        return std::max(last_us_ + (frequency_ == 200 ? 3000 : 10000), phase_us_);
    }
    /** 本地成功交付一帧后推进一次，不得对失败交付或历史周期重复调用。 */
    void sent(std::int64_t now_us) {
        const auto interval = frequency_ == 200 ? 5000 : 13334;
        // 名义相位和实际最短间隔分别推进。晚唤醒时只延后间隔，不能把误差反复写回相位。
        // 3ms 下限给 200Hz 留出 2ms 调度余量；每个补偿时隙重新采样，不重发历史按键或位移。
        phase_us_ = !phase_us_ || now_us - last_us_ > 4 * interval
                        ? now_us + interval
                        : std::max(phase_us_ + interval, now_us - 3 * interval);
        last_us_ = now_us;
    }

   private:
    int frequency_ = 0;
    std::int64_t phase_us_ = 0, last_us_ = 0;
};
