#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "client_to_server.pb.h"
#include "input/input_state.h"

/** MQTT 线程独占的累计量水位；仅成功交付后推进，分数灵敏度余量跨周期保留。
 * 不排队历史帧；代次变化清水位，失效只生成一次中立帧。单位为每发布周期相对增量。
 */
class InputPacket {
   public:
    /** 生成当前帧，不改变已发送水位；false 表示当前没有有效控制或待释放状态。 */
    bool prepare(const InputState::Sample& sample,
                 rm_client_to_server::KeyboardMouseControl& message) {
        current_ = sample;
        const bool control = sample.valid && sample.mode == InputState::Mode::Control;
        if (!control && !was_sending_) return false;
        neutral_ = !control;
        if (sample.generation != generation_) {
            x_ = y_ = wheel_ = residual_x_ = residual_y_ = residual_wheel_ = 0;
            generation_ = sample.generation;
        }
        message.Clear();
        next_x_ = control ? (sample.x - x_) * sample.sensitivity + residual_x_ : 0;
        // 官方坐标向上为正；本地原始输入向下为正，反转偏好在此处只应用一次。
        next_y_ = control ? (sample.y - y_) * sample.sensitivity * (sample.invert_y ? 1 : -1) +
                                residual_y_
                          : 0;
        next_wheel_ = control ? sample.wheel - wheel_ + residual_wheel_ : 0;
        message.set_mouse_x(integer(next_x_));
        message.set_mouse_y(integer(next_y_));
        message.set_mouse_z(integer(next_wheel_));
        message.set_keyboard_value(control ? sample.keyboard : 0);
        message.set_left_button_down(control && (sample.buttons & 1));
        message.set_right_button_down(control && (sample.buttons & 2));
        message.set_mid_button_down(control && (sample.buttons & 4));
        return true;
    }
    /** 必须与最近 prepare 对应，只有本地 publish 成功时调用。 */
    void commit() {
        was_sending_ = !neutral_;
        x_ = current_.x;
        y_ = current_.y;
        wheel_ = current_.wheel;
        residual_x_ = next_x_ - std::trunc(next_x_);
        residual_y_ = next_y_ - std::trunc(next_y_);
        residual_wheel_ = next_wheel_ - std::trunc(next_wheel_);
    }
    /** 断线后清除发送历史，禁止在重连后补发旧状态或旧释放帧。 */
    void reset() { *this = {}; }
    /** 最近生成帧是否为释放帧，仅用于诊断。 */
    bool neutral() const { return neutral_; }

   private:
    static qint32 integer(double value) {
        return qint32(std::clamp(value, double(std::numeric_limits<qint32>::min()),
                                 double(std::numeric_limits<qint32>::max())));
    }
    InputState::Sample current_;
    quint64 generation_ = 0;
    bool was_sending_ = false, neutral_ = false;
    double x_ = 0, y_ = 0, wheel_ = 0;
    double residual_x_ = 0, residual_y_ = 0, residual_wheel_ = 0;
    double next_x_ = 0, next_y_ = 0, next_wheel_ = 0;
};
