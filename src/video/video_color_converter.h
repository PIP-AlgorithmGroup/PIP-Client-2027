#pragma once

#include <QImage>
#include <QString>
#include <memory>

struct AVFrame;

/** 单解码线程拥有的 CPU 视频转色器；输入帧仅在 convert 期间借用，不跨线程持有。
 * 输出为独立 RGBA8888 图像，按流矩阵/范围转换，未标注的 HD 沿用 BT.709。
 * NV12 先无损拆平面，利用 FFmpeg 平面 YUV 的等尺寸转换路径，不缩放画面。
 */
class VideoColorConverter {
   public:
    /** 构造空缓存；按首帧规格懒分配，失败由 convert/error 报告。 */
    VideoColorConverter();
    /** 在所属线程释放 swscale 上下文和中间帧；不得与 convert 并发。 */
    ~VideoColorConverter();
    /** 借用一个有效软件 AVFrame，返回独立图像；内存/格式错误返回空并设置 error。 */
    QImage convert(const AVFrame* frame);
    /** 最近转换错误；成功清空，仅在所属线程读取。 */
    QString error() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
