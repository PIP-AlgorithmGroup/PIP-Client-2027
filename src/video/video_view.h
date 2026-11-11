#pragma once

#include <QtQml/qqmlregistration.h>

#include <QPointer>
#include <QQuickItem>

#include "video_receiver.h"

/** Qt 场景图视频项；GUI 设置最新只读图，渲染同步阶段上传纹理，不依赖厂商图形 API。 */
class VideoView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    /// 不接管接收器所有权；接收器应比 QML engine 存活更久，销毁时自动解除连接。
    Q_PROPERTY(VideoReceiver* receiver READ receiver WRITE setReceiver NOTIFY receiverChanged)
    /// 逻辑像素坐标，包含 SAR 的等比居中区域；空图时为空。
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
   public:
    /** 创建空视频项；只从 GUI 线程调用。 */
    explicit VideoView(QQuickItem* parent = nullptr);
    /** 取得接收器和当前适配区域，不触发 I/O。 */
    VideoReceiver* receiver() const;
    QRectF contentRect() const;
    /** 切换帧来源并清除旧画面；重复赋值幂等。 */
    void setReceiver(VideoReceiver* receiver);

   signals:
    /** 来源对象发生改变或被销毁。 */
    void receiverChanged();
    /** 图像比例、有效性或控件几何发生变化。 */
    void contentRectChanged();

   protected:
    /** Qt 渲染同步调用，GUI 被阻塞；纹理由返回的场景节点独占。 */
    QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) override;
    /** 窗口调整只修改显示区域，不修改源画面或 UI 档位。 */
    void geometryChange(const QRectF& new_geometry, const QRectF& old_geometry) override;

   private:
    void refresh();
    QPointer<VideoReceiver> receiver_;
    VideoFrame frame_;
    quint64 revision_ = 0;
    QMetaObject::Connection frame_connection_;
    QMetaObject::Connection destroy_connection_;
};
