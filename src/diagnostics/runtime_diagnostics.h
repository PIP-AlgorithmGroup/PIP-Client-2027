#pragma once

#include <QObject>

#include "video/video_statistics.h"

class AppController;
class DataPipeline;
class MatchSession;
class QQuickWindow;
class SessionLog;
class VideoReceiver;

/**
 * GUI 线程诊断观察器，只读取业务公开快照，不改变会话/视频生命周期。
 * 状态变化即时记事件，每秒写有界快照；不记录包内容或逐帧日志。
 * 由启动栈持有，在窗口和被观察对象销毁前析构，日志写线程比本对象活得更久。
 */
class RuntimeDiagnostics : public QObject {
   public:
    /** 注入的引用/窗口均不转移所有权，必须在本对象生存期内有效。 */
    RuntimeDiagnostics(SessionLog& log, AppController& controller, DataPipeline& pipeline,
                       VideoReceiver& video, MatchSession& session, QQuickWindow& window);

   private:
    VideoTimingStatistics heartbeat_timings_;  ///< GUI 独有；20ms 心跳超时按最近 1 秒聚合。
};
