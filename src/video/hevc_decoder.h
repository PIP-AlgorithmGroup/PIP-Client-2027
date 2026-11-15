#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVariantList>
#include <memory>

/** 一个完整解码画面；RGBA8888 CPU 内存，隐式共享，只读交给 GUI，无跨线程 AVFrame。 */
struct VideoFrame {
    QImage image;
    double pixel_aspect = 1.0;    ///< 样本宽高比；显示宽高比 = 像素宽高比 × 图像宽高比。
    qint64 assembled_us = -1;     ///< 完整 AU 在本机重组时刻；单调微秒，不是摄像头采集时刻。
    qint64 decoded_us = -1;       ///< RGBA 输出完成时刻；与 assembled_us 共用稳态时基。
    qint64 first_packet_us = -1;  ///< 本帧首个有效分片被应用读出时刻；不是内核到包时间。
    qint64 gui_received_us = -1;  ///< GUI 从邮箱取走本帧的单调微秒；只由 GUI 赋值。
    int frame_id = -1;            ///< 输出 AU 的 16 位协议编号，未知为 -1；不参与播放调度。
};

/** 一次 decode 调用各阶段的同步 CPU 观测耗时，单位微秒；-1 表示阶段未执行。
 * codec 包括 send/receive 及其驱动等待；download 包括硬件同步；convert 包括分配与转色。
 */
struct VideoDecodeTimings {
    qint64 total_us = -1;  ///< 整次 decode 同步调用，包含 packet 准备、回退、下载和转色。
    qint64 codec_us = -1;
    qint64 download_us = -1;
    qint64 convert_us = -1;
};

/** FFmpeg 7 HEVC 自动硬解及 CPU 回退；所有方法和析构必须在同一解码线程调用。 */
class HevcDecoder {
   public:
    /** auto 探测平台设备；cpu 强制软件；selected + 设备身份 key 精确选卡；也可指定后端/驱动编号。
     * 指定硬件失败仍回退 CPU，原因由 fallbackReason 返回；不将设备初始化视为解码成功。
     */
    explicit HevcDecoder(const QString& backend = "auto", const QString& device = {});
    /** 释放解码器、帧和色彩转换器；须先停止所有 decode 调用。 */
    ~HevcDecoder();
    /** 输入完整 Annex B AU；assembled_us 为本机重组单调微秒，未知为 -1。
     * first_packet_us 为首个有效分片读出时刻，frame_id 为协议帧编号，未知均为 -1；
     * 元数据随 packet 关联到实际输出，不能把重排后的画面误标成最新输入。
     * 时间不用于播放排队，不能把缓冲输出误关联到最新输入；只返回最新图像。
     */
    VideoFrame decode(const QByteArray& access_unit, qint64 assembled_us = -1,
                      qint64 first_packet_us = -1, int frame_id = -1);
    /** 最近初始化或解码错误；成功输出画面时清空。 */
    QString error() const;
    /** 清除流参数和参考帧，保留选定设备及失败历史，不重新枚举；下一帧需参数集/关键帧。 */
    void reset();
    /** 实际产生硬件帧后才为 true；设备选择中为 false。 */
    bool hardwareActive() const;
    /** CPU、待验证的后端名加 ?，或实际已输出的硬件后端名。 */
    QString backend() const;
    /** 当前设备的只读标识；不向 GUI 暴露原生句柄。 */
    QString device() const;
    /** 本会话按候选顺序记录的失败原因；成功输出也保留，主动 cpu 时为空。 */
    QString fallbackReason() const;
    /** 最近一次 decode 的阶段成本；只在所属解码线程读取，失败/重试也计入同次调用。 */
    VideoDecodeTimings timings() const;
    /** 枚举实际 DXGI / VAAPI / CUDA 设备并逐个初始化 HEVC，必须在专用探测线程调用。
     * 返回 key/backend/device/label/available/reason/hardwareActive；初始化通过不代表实流通过。
     * CPU 始终列出；驱动内部调用不可强制取消，析构前必须 join 探测线程。
     */
    static QVariantList probeDevices();

   private:
    struct Impl;
    VideoFrame decodeAttempt(const QByteArray& access_unit, qint64 assembled_us,
                             qint64 first_packet_us, int frame_id);
    std::unique_ptr<Impl> impl_;
};
