#include "session_log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace {
// handler 锁只保护实例指针的寿命，不执行磁盘 I/O；递归消息丢弃以避免日志自身死循环。
std::mutex handler_mutex;
SessionLog* active_log = nullptr;
QtMessageHandler previous_handler = nullptr;
thread_local bool handling_message = false;

QString sanitize(QString text) {
    text = text.left(4096);
    static const QRegularExpression secrets(
        R"((\b(?:password|passwd|token|api[_-]?key|secret|authorization)\b["']?\s*[=:]\s*)(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[^\s,;]+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bearer(R"(\bBearer\s+[^\s,;]+)",
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression user_info(R"((\b[a-z][a-z0-9+.-]*://)[^/\s@]+@)",
                                              QRegularExpression::CaseInsensitiveOption);
    text.replace(secrets, "\\1[redacted]");
    text.replace(bearer, "Bearer [redacted]");
    text.replace(user_info, "\\1[redacted]@");
    return text;
}

QJsonValue safeValue(const QString& key, const QJsonValue& value, int depth = 0) {
    static const QRegularExpression secret_key(
        "password|passwd|token|api[_-]?key|secret|authorization",
        QRegularExpression::CaseInsensitiveOption);
    if (secret_key.match(key).hasMatch()) return "[redacted]";
    if (depth > 5) return "[depth limit]";
    if (value.isString()) return sanitize(value.toString());
    if (value.isObject()) {
        QJsonObject safe;
        const auto object = value.toObject();
        int count = 0;
        for (auto it = object.begin(); it != object.end() && count++ < 64; ++it)
            safe.insert(it.key().left(128), safeValue(it.key(), it.value(), depth + 1));
        return safe;
    }
    if (value.isArray()) {
        QJsonArray safe;
        const auto array = value.toArray();
        for (int i = 0; i < qMin(array.size(), qsizetype(64)); ++i)
            safe.append(safeValue({}, array.at(i), depth + 1));
        return safe;
    }
    return value;
}

// 按最终 UTF-8/JSON 字节限长；超大内容保留时基和线程，明确标记被省略字段。
QByteArray encodeBoundedEvent(QJsonObject event, qint64 maximum_bytes) {
    auto bytes = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (bytes.size() <= maximum_bytes) return bytes;
    event.remove("fields");
    event.insert("fieldsOmitted", true);
    for (int limit = 256;; limit /= 2) {
        event.insert("message", event.value("message").toString().left(limit));
        event.insert("category", event.value("category").toString().left(qMin(limit, 64)));
        bytes = QJsonDocument(event).toJson(QJsonDocument::Compact);
        if (bytes.size() <= maximum_bytes) return bytes;
        // 最大 16 字符 level 与固定元数据不足 1 KiB，limit=0 前必定满足最小文件预算。
    }
}

void qtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (handling_message) return;
    handling_message = true;
    QtMessageHandler forward;
    {
        std::lock_guard lock(handler_mutex);
        forward = previous_handler;
        if (active_log) {
            // 队列已满时先让最后一批普通事件落盘，避免关键 fatal 消息直接被丢弃。
            if (type == QtFatalMsg) active_log->flush(500);
            const auto level = type == QtDebugMsg     ? "debug"
                               : type == QtInfoMsg    ? "info"
                               : type == QtWarningMsg ? "warning"
                               : type == QtFatalMsg   ? "fatal"
                                                      : "error";
            active_log->record(level, QString::fromUtf8(context.category), message,
                               {{"source", QFileInfo(QString::fromUtf8(context.file)).fileName()},
                                {"line", context.line}});
            // qFatal 的消息调用发生在 abort 之前；尽力排空，不能无限等待坏磁盘或死线程。
            if (type == QtFatalMsg) active_log->flush(500);
        }
    }
    if (forward) {
        forward(type, context, sanitize(message));
    } else {
        const auto bytes = sanitize(message).toUtf8();
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
        std::fwrite("\n", 1, 1, stderr);
    }
    handling_message = false;
    // Qt Windows fatal 路径可直接终止进程；明确 abort 才会进入本程序的紧急崩溃处理器。
    if (type == QtFatalMsg) std::abort();
}
}  // namespace

struct SessionLog::Impl {
    Options options;
    QString root;
    QString directory;
    QString failure;
    std::unique_ptr<QLockFile> session_lock;
    std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable drained;
    std::deque<QByteArray> queue;
    std::thread writer;
    quint64 dropped = 0;
    bool writing = false;
    bool stopping = false;
    bool finished = false;

    bool state(const QString& status, int exit_code = 0) {
        QSaveFile file(QDir(directory).filePath("session.json"));
        const auto bytes =
            QJsonDocument(QJsonObject{{"state", status},
                                      {"exitCode", exit_code},
                                      {"updatedUtc", QDateTime::currentDateTimeUtc().toString(
                                                         Qt::ISODateWithMs)}})
                .toJson(QJsonDocument::Indented);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
               file.commit();
    }

    void prune() {
        QDir logs(root);
        auto sessions = logs.entryInfoList({"session_*"}, QDir::Dirs | QDir::NoDotAndDotDot,
                                           QDir::Name | QDir::Reversed);
        // 仅删除本模块命名的会话，拒绝符号链接；锁定的其他进程会话绝不删除。
        static const QRegularExpression session_name(
            "\\Asession_[0-9]{8}T[0-9]{9}_[0-9]+_[0-9a-f]{8}\\z");
        sessions.removeIf([&](const QFileInfo& entry) {
            return entry.isSymLink() || !session_name.match(entry.fileName()).hasMatch();
        });
        for (int i = options.retained_sessions; i < sessions.size(); ++i) {
            const auto& entry = sessions.at(i);
            if (entry.absoluteFilePath() == directory) continue;
            QLockFile candidate(QDir(entry.absoluteFilePath()).filePath("active.lock"));
            candidate.setStaleLockTime(0);
            if (candidate.tryLock(0)) {
                // 会话名称不可重用；确认无人使用后释放 Windows 文件锁再删除旧目录。
                candidate.unlock();
                QDir(entry.absoluteFilePath()).removeRecursively();
            }
        }
    }

    void writeLoop() {
        QFile file(QDir(directory).filePath("events_0.jsonl"));
        auto open_file = [&] {
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                return true;
            }
            std::lock_guard lock(mutex);
            failure = "日志文件无法写入：" + file.errorString();
            return false;
        };
        open_file();
        while (true) {
            QByteArray bytes;
            quint64 lost = 0;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !queue.empty(); });
                if (queue.empty() && stopping) break;
                bytes = std::move(queue.front());
                queue.pop_front();
                lost = std::exchange(dropped, 0);
                writing = true;
            }
            // 溢出信息附在同一 JSON 记录上，避免额外前缀突破单文件容量上限。
            if (lost) {
                auto event = QJsonDocument::fromJson(bytes).object();
                event.insert("droppedRecords", qint64(lost));
                bytes = encodeBoundedEvent(event, options.file_bytes - 1) + '\n';
            }
            if (file.isOpen() && file.size() + bytes.size() > options.file_bytes) {
                file.close();
                QDir logs(directory);
                const auto oldest = QString("events_%1.jsonl").arg(options.files_per_session - 1);
                bool rotated = !logs.exists(oldest) || logs.remove(oldest);
                for (int i = options.files_per_session - 2; i >= 0 && rotated; --i) {
                    const auto source = QString("events_%1.jsonl").arg(i);
                    rotated = !logs.exists(source) ||
                              logs.rename(source, QString("events_%1.jsonl").arg(i + 1));
                }
                if (rotated) {
                    open_file();
                } else {
                    std::lock_guard lock(mutex);
                    failure = "日志轮转失败，已停止写入以保持容量上限";
                }
            }
            if (file.isOpen() && (file.write(bytes) != bytes.size() || !file.flush())) {
                std::lock_guard lock(mutex);
                failure = "日志写入失败：" + file.errorString();
                file.close();
            }
            {
                std::lock_guard lock(mutex);
                writing = false;
                if (queue.empty()) drained.notify_all();
            }
        }
    }
};

SessionLog::SessionLog() : SessionLog(Options{}) {}
SessionLog::SessionLog(const Options& options) : impl_(std::make_unique<Impl>()) {
    auto& state = *impl_;
    state.options = options;
    state.options.file_bytes = std::max(qint64(1024), options.file_bytes);
    state.options.files_per_session = qBound(1, options.files_per_session, 16);
    state.options.retained_sessions = qBound(1, options.retained_sessions, 100);
    state.options.queue_records = qBound(1, options.queue_records, 8192);
    state.root = options.root_directory.isEmpty()
                     ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                           .filePath("logs")
                     : QDir(options.root_directory).absolutePath();
    const auto name = "session_" + QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz") +
                      "_" + QString::number(QCoreApplication::applicationPid()) + "_" +
                      QUuid::createUuid().toString(QUuid::Id128).left(8);
    state.directory = QDir(state.root).filePath(name);
    if (!QDir().mkpath(state.directory)) {
        state.failure = "无法创建本地日志目录";
        return;
    }
    state.session_lock = std::make_unique<QLockFile>(QDir(state.directory).filePath("active.lock"));
    state.session_lock->setStaleLockTime(0);
    if (!state.session_lock->tryLock(0) || !state.state("active")) {
        state.failure = "无法初始化本地日志会话";
        return;
    }
    state.prune();
    state.writer = std::thread([&state] { state.writeLoop(); });
    record("info", "application", "session_started",
           {{"version", QCoreApplication::applicationVersion()},
            {"qtVersion", QString::fromLatin1(qVersion())},
            {"os", QSysInfo::prettyProductName()},
            {"architecture", QSysInfo::currentCpuArchitecture()},
            {"buildCpu", QSysInfo::buildCpuArchitecture()},
            {"pid", QCoreApplication::applicationPid()}});
}

SessionLog::~SessionLog() {
    auto& state = *impl_;
    {
        std::lock_guard lock(handler_mutex);
        if (active_log == this) {
            qInstallMessageHandler(previous_handler);
            active_log = nullptr;
            previous_handler = nullptr;
        }
    }
    {
        std::lock_guard lock(state.mutex);
        state.stopping = true;
    }
    state.wake.notify_one();
    if (state.writer.joinable()) state.writer.join();
}

QString SessionLog::sessionDirectory() const { return impl_->directory; }
QString SessionLog::rootDirectory() const { return impl_->root; }
QString SessionLog::error() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->failure;
}
bool SessionLog::installQtHandler() {
    std::lock_guard lock(handler_mutex);
    if (active_log == this) return true;
    if (active_log || !error().isEmpty()) return false;
    previous_handler = qInstallMessageHandler(qtMessage);
    active_log = this;
    return true;
}

void SessionLog::record(const QString& level, const QString& category, const QString& message,
                        const QJsonObject& fields) {
    auto& state = *impl_;
    auto safe_fields = safeValue({}, fields).toObject();
    auto event =
        QJsonObject{{"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                    {"uptimeUs", qint64(std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::steady_clock::now() - state.origin)
                                            .count())},
                    {"thread", QString::number(quintptr(QThread::currentThreadId()), 16)},
                    {"level", level.left(16)},
                    {"category", category.left(128)},
                    {"message", sanitize(message)},
                    {"fields", safe_fields}};
    auto bytes = encodeBoundedEvent(event, qMin(qint64(16384), state.options.file_bytes - 1));
    bytes.append('\n');
    {
        std::lock_guard lock(state.mutex);
        if (state.stopping || !state.writer.joinable()) return;
        if (state.queue.size() >= size_t(state.options.queue_records)) {
            ++state.dropped;
            if (level == "fatal")
                state.queue.pop_back();
            else
                return;
        }
        state.queue.push_back(std::move(bytes));
    }
    state.wake.notify_one();
}

bool SessionLog::flush(int timeout_ms) {
    auto& state = *impl_;
    std::unique_lock lock(state.mutex);
    return state.drained.wait_for(lock, std::chrono::milliseconds(qMax(0, timeout_ms)), [&] {
        return state.queue.empty() && !state.writing;
    }) && state.failure.isEmpty();
}

bool SessionLog::finish(int exit_code) {
    auto& state = *impl_;
    if (state.finished) return error().isEmpty();
    // 退出时生产者已停止；先腾空有界队列，保证结束事件不会因最后一批高负载而被丢弃。
    if (!flush()) return false;
    record("info", "application", "session_finished", {{"exitCode", exit_code}});
    if (!flush()) return false;
    state.finished = state.state("finished", exit_code);
    if (!state.finished) {
        std::lock_guard lock(state.mutex);
        state.failure = "无法写入日志会话退出标记";
    }
    return state.finished;
}
