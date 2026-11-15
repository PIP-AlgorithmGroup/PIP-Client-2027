#pragma once

#include <QtQml/qqmlregistration.h>

#include <QPointer>
#include <QQuickItem>
#include <QTimer>

#include "video_receiver.h"

struct VideoPresentationStatistics;

/** Qt 场景图视频项；GUI 设置最新只读图，渲染同步阶段上传纹理，不依赖厂商图形 API。 */
class VideoView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    /// 不接管接收器所有权；接收器应比 QML engine 存活更久，销毁时自动解除连接。
    Q_PROPERTY(VideoReceiver* receiver READ receiver WRITE setReceiver NOTIFY receiverChanged)
    /// 逻辑像素坐标，包含 SAR 的等比居中区域；空图时为空。
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
    /// 最近 1 秒含新视频纹理的 Qt frameSwapped 次数；不是显示器扫描或视频接收 FPS。
    Q_PROPERTY(double presentationFps READ presentationFps NOTIFY presentationFpsChanged)
    /// GUI 只读值副本；最近 10 秒新视频呈交间隔的 5/1% low，20/100 样本预热。
    /// 无图时为空；统计 Qt frameSwapped，不代表显示器实际扫描。
    Q_PROPERTY(QVariantMap presentationDiagnostics READ presentationDiagnostics NOTIFY
                   presentationFpsChanged)
   public:
    /** 创建空视频项；只从 GUI 线程调用。 */
    explicit VideoView(QQuickItem* parent = nullptr);
    /** 取得接收器和当前适配区域，不触发 I/O。 */
    VideoReceiver* receiver() const;
    QRectF contentRect() const;
    /** 切换帧来源并清除旧画面；重复赋值幂等。 */
    void setReceiver(VideoReceiver* receiver);
    /** GUI 线程读取；预热/无呈交来源时返回 -1。 */
    double presentationFps() const;
    /** GUI 线程读取呈交间隔及 timings 阶段统计；1 秒均值/最大/P95/P99，空阶段为 -1。
     * texture 仅 CPU 准备/提交耗时；local 从完整组帧起算，receiveLocal 从应用首包读出起算。
     * 两者终点均为 Qt 呈交，不含物理显示扫描。
     * mailboxWait / guiPrepare / renderWait 将 guiWait 拆为邮箱交接、GUI 处理与渲染调度。
     */
    QVariantMap presentationDiagnostics() const;

   signals:
    /** 来源对象发生改变或被销毁。 */
    void receiverChanged();
    /** 图像比例、有效性或控件几何发生变化。 */
    void contentRectChanged();
    /** 周期统计最多每 250ms 一次；清屏可立即发射。仅 GUI 线程发射。 */
    void presentationFpsChanged();

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
    qint64 update_requested_us_ = -1;  ///< 当前 revision 请求刷新的稳态微秒；GUI 写、渲染同步读。
    QMetaObject::Connection frame_connection_;
    QMetaObject::Connection destroy_connection_;
    QMetaObject::Connection swap_connection_;
    // frameSwapped 可能在渲染线程；回调只持共享统计，不引用已析构的 VideoView。
    std::shared_ptr<VideoPresentationStatistics> presentation_statistics_;
    QTimer statistics_timer_;
    double presentation_fps_ = -1;
    QVariantMap presentation_diagnostics_;  ///< GUI 独有快照，每 250ms 从受锁统计复制。
};
