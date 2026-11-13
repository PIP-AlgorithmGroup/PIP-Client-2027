#pragma once

#include <QJsonObject>
#include <QString>
#include <memory>

/**
 * 每次启动独立的本地 JSONL 日志。GUI 线程在 QGuiApplication 前创建，退出后最后销毁。
 * record 可由任意线程调用，只入有界队列；磁盘由专属线程写，超额记录计数而不阻塞实时路径。
 * 同进程仅一个实例可接管 Qt 消息；析构前调用者必须停止自己的日志生产线程。
 */
class SessionLog {
   public:
    /** 容量单位为字节；小值供测试注入，产品使用默认预算。 */
    struct Options {
        QString root_directory;  ///< 空值为 Qt AppLocalDataLocation/logs，不使用安装目录或 cwd。
        qint64 file_bytes = 8 * 1024 * 1024;
        int files_per_session = 4;
        int retained_sessions = 10;
        int queue_records = 1024;
    };

    /** 建目录/锁会话失败时保留 error，程序可继续启动；不抛出磁盘错误。 */
    explicit SessionLog(const Options& options);
    SessionLog();
    /** 恢复原 Qt handler，停止并 join 写线程；未 finish 的会话保留 active 标记。 */
    ~SessionLog();
    SessionLog(const SessionLog&) = delete;
    SessionLog& operator=(const SessionLog&) = delete;

    /** 当前会话目录/日志根目录与初始化或写入错误；返回值副本，线程安全。 */
    QString sessionDirectory() const;
    QString rootDirectory() const;
    QString error() const;
    /** 仅启动线程调用一次；捕获后续 Qt/QML 日志，保留先前 handler 的控制台行为。 */
    bool installQtHandler();
    /**
     * level/category 为固定本地标识；fields 不得包含协议原文、视频或凭证。
     * 文本截断为有界长度，常见凭证字段脱敏；仍由调用者负责不提交原始敏感负载。
     */
    void record(const QString& level, const QString& category, const QString& message,
                const QJsonObject& fields = {});
    /** 等待已入队记录落盘，超时毫秒；不等价于断电持久性，失败返回 false。 */
    bool flush(int timeout_ms = 2000);
    /** 资源正常释放后调用一次；排空队列并原子写 finished/exitCode，返回写入是否成功。 */
    bool finish(int exit_code);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
