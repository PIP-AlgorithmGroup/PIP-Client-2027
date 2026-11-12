#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <memory>

/** 一个完整解码画面；RGBA8888 CPU 内存，隐式共享，只读交给 GUI，无跨线程 AVFrame。 */
struct VideoFrame {
    QImage image;
    double pixel_aspect = 1.0;  ///< 样本宽高比；显示宽高比 = 像素宽高比 × 图像宽高比。
};

/** FFmpeg 7 HEVC 自动硬解及 CPU 回退；所有方法和析构必须在同一接收线程调用。 */
class HevcDecoder {
   public:
    /** auto 探测平台设备；cpu 强制软件；也可指定 FFmpeg 后端和设备标识。
     * 指定硬件失败仍回退 CPU，原因由 fallbackReason 返回；不将设备初始化视为解码成功。
     */
    explicit HevcDecoder(const QString& backend = "auto", const QString& device = {});
    /** 释放解码器、帧和色彩转换器；须先停止所有 decode 调用。 */
    ~HevcDecoder();
    /** 输入完整 Annex B HEVC 访问单元；只返回最新输出，无输出时返回空图。 */
    VideoFrame decode(const QByteArray& access_unit);
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

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
