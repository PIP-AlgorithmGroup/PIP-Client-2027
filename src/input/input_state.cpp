#include "input/input_state.h"

#include <QStringList>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
// 官方 bit 0..15 的物理键；左右 Shift/Ctrl 在 sample 中合并，内部仍独立保存。
constexpr int kProtocolKeys[] = {17, 31, 30, 32, 42, 29, 16, 18, 19, 33, 34, 44, 45, 46, 47, 48};
}  // namespace

qint64 InputState::nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void InputState::Rate::add(qint64 now_us) {
    const auto slot = now_us / 10000;
    auto& bucket = buckets[slot % buckets.size()];
    if (bucket.slot != slot) bucket = {slot, 0};
    ++bucket.count;
    ++count;
    if (last_us && now_us > last_us && now_us - last_us < 1000000) {
        intervals[cursor] = now_us - last_us;
        interval_times[cursor] = now_us;
        cursor = (cursor + 1) % intervals.size();
        samples = std::min(samples + 1, int(intervals.size()));
    }
    last_us = now_us;
}
QVariantMap InputState::Rate::values(qint64 now_us) const {
    quint64 rate = 0;
    for (const auto& bucket : buckets)
        if (bucket.slot <= now_us / 10000 && bucket.slot > now_us / 10000 - 100)
            rate += bucket.count;
    double mean = 0, minimum = 0, maximum = 0, p99 = 0;
    int fresh = 0;
    auto sorted = intervals;
    for (int i = 0; i < samples; ++i)
        if (now_us >= interval_times[i] && now_us - interval_times[i] < 1000000)
            sorted[fresh++] = intervals[i];
    if (fresh) {
        std::sort(sorted.begin(), sorted.begin() + fresh);
        for (int i = 0; i < fresh; ++i) mean += sorted[i] / 1000.0 / fresh;
        minimum = sorted[0] / 1000.0;
        maximum = sorted[fresh - 1] / 1000.0;
        p99 = sorted[int(std::ceil(fresh * .99)) - 1] / 1000.0;
    }
    return {{"hz", double(rate)},
            {"count", qulonglong(count)},
            {"meanMs", mean},
            {"minMs", minimum},
            {"maxMs", maximum},
            {"p99Ms", p99},
            {"activeHz", mean > 0 ? 1000.0 / mean : 0},
            {"samples", fresh}};
}
void InputState::clearCapture() {
    ++state_.generation;
    state_.valid = false;
    state_.locked = false;
    state_.x = state_.y = state_.wheel = 0;
    suppressed_ = devices_;
}
void InputState::request(Mode mode, bool lock_pointer, double sensitivity, bool invert_y,
                         int frequency) {
    std::lock_guard lock(mutex_);
    if (mode != state_.mode || lock_pointer != wants_lock_ || interrupted_) clearCapture();
    state_.mode = mode;
    wants_lock_ = lock_pointer;
    interrupted_ = false;
    state_.sensitivity = sensitivity;
    state_.invert_y = invert_y;
    state_.frequency = frequency;
}
void InputState::capture(bool focused, bool locked, qint64 now_us) {
    std::lock_guard lock(mutex_);
    if (heartbeat_us_ && now_us - heartbeat_us_ > 25000) clearCapture();
    const bool valid = !interrupted_ && state_.mode != Mode::Off &&
                       (state_.mode != Mode::Control || transport_ready_) && focused &&
                       (!wants_lock_ || locked);
    if (state_.valid && !valid) clearCapture();
    focused_ = focused;
    state_.valid = valid;
    state_.locked = locked && valid;
    heartbeat_us_ = now_us;
}
void InputState::key(quint64 device, int code, bool down, qint64 now_us) {
    if (code < 0 || code > 767) return;
    std::lock_guard lock(mutex_);
    if (!devices_.contains(device) && devices_.size() >= 16) {
        ++rejected_devices_;
        return;
    }
    if (!down && device != 0) {
        if (auto aggregate = devices_.find(0); aggregate != devices_.end())
            aggregate->second.erase(code);
        if (auto aggregate = suppressed_.find(0); aggregate != suppressed_.end())
            aggregate->second.erase(code);
    }
    auto& keys = devices_[device];
    if (keys.contains(code) == down && down) {
        ++repeats_;
        return;
    }
    if (down)
        keys.insert(code);
    else {
        keys.erase(code);
        suppressed_[device].erase(code);
    }
    if (state_.mode != Mode::Off && focused_) {
        state_.last_event_us = now_us;
        if ((code < 256 || code >= 352)) keyboard_rate_.add(now_us);
    }
    // Esc 撤销不依赖 GUI 派发；菜单中观察测试时同样退出锁定，但 Qt 仍收到正常事件。
    if (code == 1 && down && state_.valid) {
        interrupted_ = true;
        clearCapture();
    }
}
void InputState::motion(double x, double y, double wheel, qint64 now_us) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(wheel)) return;
    std::lock_guard lock(mutex_);
    if (state_.mode == Mode::Off || !focused_) return;
    state_.last_event_us = now_us;
    if (state_.valid) {
        // 超出实际设备能力的累计量不允许无限增长；异常时使本代失效。
        if (std::abs(state_.x + x) > 1e12 || std::abs(state_.y + y) > 1e12 ||
            std::abs(state_.wheel + wheel) > 1e12) {
            interrupted_ = true;
            clearCapture();
            return;
        }
        state_.x += x;
        state_.y += y;
        state_.wheel += wheel;
    }
}
void InputState::removeDevice(quint64 device) {
    std::lock_guard lock(mutex_);
    devices_.erase(device);
    suppressed_.erase(device);
}
void InputState::interrupt() {
    std::lock_guard lock(mutex_);
    interrupted_ = true;
    clearCapture();
}
InputState::Sample InputState::sample(qint64 now_us) const {
    std::lock_guard lock(mutex_);
    auto result = state_;
    result.valid = result.valid && now_us >= heartbeat_us_ && now_us - heartbeat_us_ <= 25000;
    std::set<int> keys;
    for (const auto& [device, values] : devices_)
        for (int key : values)
            if ((!suppressed_.contains(device) || !suppressed_.at(device).contains(key)) &&
                (!suppressed_.contains(0) || !suppressed_.at(0).contains(key)))
                keys.insert(key);
    result.keyboard = 0;
    result.buttons = 0;
    if (result.valid) {
        for (int i = 0; i < 16; ++i) {
            bool down = keys.contains(kProtocolKeys[i]);
            if (i == 4) down |= keys.contains(54);
            if (i == 5) down |= keys.contains(97);
            if (down) result.keyboard |= quint16(1 << i);
        }
        for (int i = 0; i < 3; ++i)
            if (keys.contains(272 + i)) result.buttons |= 1 << i;
    }
    return result;
}
bool InputState::submitCurrent(const Sample& sample, const std::function<bool()>& submit) {
    std::lock_guard lock(mutex_);
    if (sample.generation != state_.generation || sample.mode != state_.mode) return false;
    if (sample.valid &&
        (!state_.valid || !transport_ready_ || interrupted_ || nowUs() - heartbeat_us_ > 25000))
        return false;
    return submit();
}
QVariantMap InputState::diagnostics(qint64 now_us) const {
    std::lock_guard lock(mutex_);
    QVariantList keys;
    std::set<int> all;
    for (const auto& [device, values] : devices_) all.insert(values.begin(), values.end());
    QStringList key_names, mouse_names;
    for (const auto key : all) {
        keys.append(key);
        if (key >= 272 && key <= 276)
            mouse_names.append(keyName(key));
        else
            key_names.append(keyName(key));
    }
    return {{"backend", backend_},
            {"error", error_},
            {"keys", keys},
            {"pressedKeys", key_names},
            {"pressedMouse", mouse_names},
            {"keyboard", keyboard_rate_.values(now_us)},
            {"mouse", mouse_rate_.values(now_us)},
            {"x", state_.x},
            {"y", state_.y},
            {"wheel", state_.wheel},
            {"send", send_rate_.values(now_us)},
            {"neutralCount", qulonglong(neutral_count_)},
            {"repeats", qulonglong(repeats_)},
            {"rejectedDevices", qulonglong(rejected_devices_)}};
}
void InputState::backend(const QString& name, const QString& error) {
    std::lock_guard lock(mutex_);
    backend_ = name;
    error_ = error;
}
void InputState::resetStatistics() {
    std::lock_guard lock(mutex_);
    keyboard_rate_ = {};
    mouse_rate_ = {};
    send_rate_ = {};
    neutral_count_ = 0;
    repeats_ = 0;
}
std::pair<InputState::Mode, bool> InputState::requested() const {
    std::lock_guard lock(mutex_);
    return {interrupted_ || (state_.mode == Mode::Control && !transport_ready_) ? Mode::Off
                                                                                : state_.mode,
            wants_lock_};
}

void InputState::mouseReport(qint64 now_us) {
    std::lock_guard lock(mutex_);
    if (state_.mode != Mode::Off && focused_) mouse_rate_.add(now_us);
}
void InputState::seedKey(quint64 device, int code) {
    if (code < 0 || code > 767) return;
    std::lock_guard lock(mutex_);
    if (!devices_.contains(device) && devices_.size() >= 16) return;
    devices_[device].insert(code);
    suppressed_[device].insert(code);
}
void InputState::resetDevices() {
    std::lock_guard lock(mutex_);
    devices_.clear();
    clearCapture();
}
void InputState::transport(bool ready) {
    std::lock_guard lock(mutex_);
    if (transport_ready_ != ready) clearCapture();
    transport_ready_ = ready;
}
void InputState::sent(qint64 now_us, bool neutral) {
    std::lock_guard lock(mutex_);
    send_rate_.add(now_us);
    if (neutral) ++neutral_count_;
}

QString InputState::keyName(int code) {
    // 物理名称与协议/测试视图共用；不调用 GUI 的布局翻译或文本输入接口。
    static const std::map<int, QString> names = {
        {1, "Esc"},          {14, "Backspace"},  {15, "Tab"},      {28, "Enter"}, {29, "LCtrl"},
        {42, "LShift"},      {54, "RShift"},     {56, "LAlt"},     {57, "Space"}, {58, "CapsLock"},
        {69, "NumLock"},     {70, "ScrollLock"}, {96, "NumEnter"}, {97, "RCtrl"}, {98, "Num/"},
        {99, "PrintScreen"}, {100, "RAlt"},      {102, "Home"},    {103, "Up"},   {104, "PageUp"},
        {105, "Left"},       {106, "Right"},     {107, "End"},     {108, "Down"}, {109, "PageDown"},
        {110, "Insert"},     {111, "Delete"},    {119, "Pause"},   {125, "LWin"}, {126, "RWin"},
        {127, "Menu"},       {272, "左键"},      {273, "右键"},    {274, "中键"}, {275, "侧键1"},
        {276, "侧键2"},      {12, "-"},          {13, "="},        {26, "["},     {27, "]"},
        {39, ";"},           {40, "'"},          {41, "`"},        {43, "\\"},    {51, ","},
        {52, "."},           {53, "/"},          {55, "Num*"},     {71, "Num7"},  {72, "Num8"},
        {73, "Num9"},        {74, "Num-"},       {75, "Num4"},     {76, "Num5"},  {77, "Num6"},
        {78, "Num+"},        {79, "Num1"},       {80, "Num2"},     {81, "Num3"},  {82, "Num0"},
        {83, "Num."}};
    if (const auto found = names.find(code); found != names.end()) return found->second;
    if (code >= 2 && code <= 11) return QString::number((code - 1) % 10);
    if (code >= 16 && code <= 25) return QString("QWERTYUIOP").mid(code - 16, 1);
    if (code >= 30 && code <= 38) return QString("ASDFGHJKL").mid(code - 30, 1);
    if (code >= 44 && code <= 50) return QString("ZXCVBNM").mid(code - 44, 1);
    if (code >= 59 && code <= 68) return QString("F%1").arg(code - 58);
    if (code == 87 || code == 88) return QString("F%1").arg(code - 76);
    return QString("Key%1").arg(code);
}
