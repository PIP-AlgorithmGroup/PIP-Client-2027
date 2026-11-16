#pragma once

#include <QVariantMap>
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <set>

/** 原生输入与 MQTT 共用的有界值存储；全部方法线程安全，不访问 QObject 或进行 I/O。
 * 物理键统一使用 Linux input-event-codes 编号；时间均为本机 steady_clock 微秒。
 * 按键保存完整状态，运动保存本捕获代次累计量；HUD 读取不消费运动。
 */
class InputState {
   public:
    /// Test 只允许本地观察，永远不产生机器人控制消息。
    enum class Mode { Off, Control, Test };
    /** 一致快照；总移动量只在 generation 相同时可相减。 */
    struct Sample {
        Mode mode = Mode::Off;
        bool locked = false, valid = false;
        quint64 generation = 0;
        quint16 keyboard = 0;
        quint8 buttons = 0;  ///< bit 0/1/2 分别为左/右/中键。
        double x = 0, y = 0, wheel = 0, sensitivity = 1;
        bool invert_y = false;
        int frequency = 75;
        qint64 last_event_us = 0;
    };
    /** GUI 提交完整意图；改变捕获模式会抑制已按住的键直至释放，不继承旧运动。 */
    void request(Mode mode, bool lock_pointer, double sensitivity, bool invert_y, int frequency);
    /** 原生线程每次事件/健康检查调用；失焦/捕获丢失立即废弃当前控制代次。 */
    void capture(bool focused, bool locked, qint64 now_us);
    /** 单个设备物理键变化；同一批所有事件逐个调用，重复 make 不改变状态。 */
    void key(quint64 device, int code, bool down, qint64 now_us);
    /** 相对量为平台未加速量；滚轮内部统一为 120 单位/格，坐标右/下为正。 */
    void motion(double x, double y, double wheel, qint64 now_us);
    /** 原生鼠标一次交付计数；按钮/轴同包只计一次，不能当作 USB 轮询率。 */
    void mouseReport(qint64 now_us);
    /** 原生焦点恢复时初始化已按住键；不计入测试事件，并抑制到本设备释放。 */
    void seedKey(quint64 device, int code);
    /** 原生焦点重新获得时重建物理快照，旧焦点期间的释放不得遗留为按住。 */
    void resetDevices();
    /** MQTT 线程同步撤销连接资格；断线清代，不依赖 GUI 通知到达。 */
    void transport(bool ready);
    /** MQTT 线程记录本地交付；统计不表示机器人接收/执行。 */
    void sent(qint64 now_us, bool neutral);
    /** 设备移除时清除其全部键贡献，避免多个键盘或左右修饰键互相覆盖。 */
    void removeDevice(quint64 device);
    /** 立即撤销本代捕获并锁存；新 GUI 意图到达后才可再次捕获。 */
    void interrupt();
    /** 跨线程读取；输入健康超过 25ms 失效，无操作不等于健康失效。 */
    Sample sample(qint64 now_us) const;
    /** 与撤销串行校验代次并执行一次有界本地提交；submit 不得等待 I/O 或重入本对象。
     * 持锁覆盖本地 MQTT 写入，保证撤销返回后没有本代尚未写出的控制；已交给 TCP 不可撤回。
     */
    bool submitCurrent(const Sample& sample, const std::function<bool()>& submit);
    /** 只读 HUD/测试快照，最近一秒事件率及近 256 个事件间隔；不表示 USB 扫描率。 */
    QVariantMap diagnostics(qint64 now_us) const;
    /** 更新后端能力/失败信息；只用于诊断，不能据此授予捕获。 */
    void backend(const QString& name, const QString& error = {});
    /** 清空测试计数和间隔，不清除当前物理键或改变捕获资格。 */
    void resetStatistics();
    /** 后端读取所需模式和光标约束意图，二者在同一锁内取得。 */
    std::pair<Mode, bool> requested() const;
    /** 同机单调时钟；无设备时间基准混用。 */
    static qint64 nowUs();
    /** 物理键码的稳定可读名称，不受文本输入布局影响；未知键显示 Key 编号。 */
    static QString keyName(int code);

   private:
    struct Rate {
        struct Bucket {
            qint64 slot = -1;
            quint64 count = 0;
        };
        std::array<Bucket, 100> buckets{};
        std::array<qint64, 256> intervals{};
        std::array<qint64, 256> interval_times{};
        quint64 count = 0;
        qint64 last_us = 0;
        int samples = 0, cursor = 0;
        void add(qint64 now_us);
        QVariantMap values(qint64 now_us) const;
    };
    void clearCapture();
    mutable std::mutex mutex_;
    Sample state_;
    std::map<quint64, std::set<int>> devices_;  ///< 最多 16 个源、每源 768 个键。
    std::map<quint64, std::set<int>> suppressed_;
    bool wants_lock_ = false, focused_ = false, interrupted_ = false;
    bool transport_ready_ = true;
    qint64 heartbeat_us_ = 0;
    Rate keyboard_rate_, mouse_rate_, send_rate_;
    quint64 neutral_count_ = 0;
    quint64 repeats_ = 0, rejected_devices_ = 0;
    QString backend_ = "未启动", error_;
};
