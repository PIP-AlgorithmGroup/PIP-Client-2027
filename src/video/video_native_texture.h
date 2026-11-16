#pragma once

#include <QString>
#include <memory>

#include "hevc_decoder.h"

class QRhi;
class QRhiTexture;

/** 平台硬件 surface 到 Qt RHI 的适配；仅在场景图线程使用，不映射 CPU 像素。
 * D3D11 使用 keyed mutex 和有界 GPU 内复制；VAAPI 使用 DRM PRIME/EGL 平面导入。
 * 不支持的 API/设备/格式返回原因，由调用者下载并走通用显示路径。
 */
class VideoNativeTexture {
   public:
    /** 创建空缓存；首次成功导入时分配资源。 */
    VideoNativeTexture();
    /** 在场景图线程释放资源，必要时等待已提交的 GL 复制完成。 */
    ~VideoNativeTexture();
    /** 引用输入直到 GPU 安全消费；调用期间不允许并发访问同一个适配器。
     * rhi 必须属于当前渲染线程，不能跨场景图实例复用；false 时 reason 非空。
     */
    bool update(const VideoFrame& frame, QRhi* rhi, QString* reason);
    /** 成功 update 后借用的 Y/UV 纹理；有效期到规格变化或本对象销毁。 */
    QRhiTexture* plane(int index) const;
    /** 最近成功的原生互操作路径，用于诊断。 */
    QString path() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
