#pragma once

#include <algorithm>
#include <cstdint>

/** 单线程75Hz节拍器；时间为同一steady_clock的微秒数。
 * 名义周期13334us（向上取整1/75秒），帧间隔至少10000us。
 * 官方仅规定75Hz频率上限，未定义逐帧最短间隔；此处以有界相位误差整形，
 * 连续输入保留最多4周期的有限补偿预算，长空闲重定相，不生成补发帧。
 */
class ControlCadence {
   public:
    /// 返回下一次可发送的绝对微秒时间；首次可立即发送。
    std::int64_t readyAt() const { return std::max(phase_us_, gap_us_); }
    /// 实际交付后调用；now_us必须不早于readyAt()，同一帧不得重复计入预算。
    void sent(std::int64_t now_us) {
        // 相位和防突发时间必须分开；把max结果写回相位会积累每次超预算的晚唤醒。
        // 队列暂时为空不能证明源已空闲：USB下一批可能还在驱动中。
        // 短暂停顿保留有限预算，使真实新鲜帧能消除积压；超过4周期没有交付才重新定相。
        if (last_sent_us_ == 0 || now_us - last_sent_us_ > 4 * 13334)
            phase_us_ = now_us + 13334;
        else
            phase_us_ = std::max(phase_us_ + 13334, now_us - 3 * 13334);
        gap_us_ = now_us + 10000;
        last_sent_us_ = now_us;
    }

   private:
    std::int64_t phase_us_ = 0, gap_us_ = 0;
    std::int64_t last_sent_us_ = 0;
};
