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

/** FFmpeg 7 HEVC 软件解码基础路径；创建、解码、销毁必须在同一个接收线程。 */
class HevcDecoder {
   public:
    /** 分配有界软件解码资源，不创建硬件设备；失败由 error() 返回。 */
    HevcDecoder();
    /** 释放解码器、帧和色彩转换器；须先停止所有 decode 调用。 */
    ~HevcDecoder();
    /** 输入完整 Annex B HEVC 访问单元；只返回最新输出，无输出时返回空图。 */
    VideoFrame decode(const QByteArray& access_unit);
    /** 最近初始化或解码错误；成功输出画面时清空。 */
    QString error() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
