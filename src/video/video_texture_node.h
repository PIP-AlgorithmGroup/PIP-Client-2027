#pragma once

#include <QSGNode>
#include <QString>
#include <memory>

#include "hevc_decoder.h"

class QQuickWindow;

/** 渲染线程独占的视频节点；纹理随场景图销毁，GPU 导入失败时显示软件副本。
 * 不从 GUI/解码线程调用；setFrame 在 Qt 同步阶段执行，GUI 此时被阻塞。
 */
class VideoTextureNode : public QSGNode {
   public:
    /** 创建空节点；不创建 GPU 资源。 */
    VideoTextureNode();
    /** 在场景图所属线程释放纹理、互操作资源和未完成的 surface 引用。 */
    ~VideoTextureNode() override;
    /** 消费不可变帧；复用规格相同的纹理，失败返回 false；不接管窗口所有权。 */
    bool setFrame(const VideoFrame& frame, QQuickWindow* window);
    /** 设置逻辑像素中的目标矩形，不改变原始纹理尺寸。 */
    void setRect(const QRectF& rect);
    /** 最近成功显示的路径及回退原因；仅渲染线程读取，不包含厂商句柄。 */
    QString path() const;
    QString fallbackReason() const;
    /** 本帧渲染准备中的下载/CPU 转色/互操作 CPU 耗时，微秒；未执行为 -1。
     * GPU 命令异步执行，这些值不代表 GPU 时间；均包含在 texture 阶段中。
     */
    qint64 downloadUs() const;
    qint64 convertUs() const;
    qint64 bridgeUs() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
