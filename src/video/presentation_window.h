#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQuickWindow>
#include <memory>

#include "video_receiver.h"

class QQuickRenderControl;

/** 保留 Qt 输入/QML 的窗口，GPU 后端由独立线程呈交。
 * GUI 主动提交 HUD 同步，呈交线程从不请求/等待 GUI；GUI 忙时复用最后同步的场景图。
 * D3D11/OpenGL 使用同一 RHI 实现；软件/其他后端保留普通 Qt 渲染循环。
 */
class PresentationWindow : public QQuickWindow {
    Q_OBJECT
    QML_ELEMENT
    /// GUI 独有来源身份；直接信号回调仅合并唤醒，不借用 receiver 的 GUI 状态。
    Q_PROPERTY(
        VideoReceiver* videoSource READ videoSource WRITE setVideoSource NOTIFY videoSourceChanged)
    /// 选择的渲染循环，不代表指定 GPU/硬解已经验收。
    Q_PROPERTY(bool independentPresentation READ independentPresentation CONSTANT)
    /// 最近图形错误；自动重建成功后清除，连续三次失败后停用呈交并保留通信。
    Q_PROPERTY(QString presentationError READ presentationError NOTIFY presentationErrorChanged)

   public:
    /** GUI 线程构造，不创建驱动资源；首次暴露后初始化。 */
    explicit PresentationWindow(QWindow* parent = nullptr);
    /** GUI 线程同步停止呈交并释放 GPU/场景图，再释放 QML 对象；不遗留窗口借用。 */
    ~PresentationWindow() override;
    /** GUI 独有来源配置；来源对象需晚于窗口析构，销毁信号会断开唤醒。 */
    VideoReceiver* videoSource() const;
    void setVideoSource(VideoReceiver* receiver);
    /** true 代表实际选用独立循环；软件/未覆盖的 API 使用 Qt 原循环。 */
    bool independentPresentation() const;
    /** GUI 读取最近设备/交换链失败原因；不进行驱动调用。 */
    QString presentationError() const;
    /** GUI 线程显式截图；在呈交线程完成 GPU 读回，仅诊断调用，不进入常态视频路径。 */
    QImage grabFrame();

   signals:
    /** 来源对象身份已更新，仅 GUI 发射。 */
    void videoSourceChanged();
    /** 驱动/交换链失败原因改变，仅 GUI 发射；禁止静默空白。 */
    void presentationErrorChanged();
    /** 新视频帧已完成应用呈交，可能在呈交线程发射；时间均为 steady_clock 微秒。
     * picked 为呈交线程取邮箱，ready 为纹理准备完成；此信号不等于物理亮屏。
     * DirectConnection 回调必须有界且不访问 GUI，不持有任何借用纹理。
     */
    void videoFramePresented(int frame_id, qint64 first_us, qint64 decoded_us, qint64 picked_us,
                             qint64 ready_us, qint64 swap_us);

   protected:
    /** GUI 处理窗口生命周期；销毁原生 surface 前必须同步回收交换链。 */
    bool event(QEvent* event) override;

   private:
    explicit PresentationWindow(QQuickRenderControl* control);
    void scheduleSync();
    void syncScene();
    void stopRendering();
    void scheduleRecovery();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
