#include "app/session_log.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
#include <thread>
#include <vector>

namespace {
QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QByteArray events(const QString& directory) {
    QByteArray result;
    for (const auto& name : QDir(directory).entryList({"events_*.jsonl"}, QDir::Files, QDir::Name))
        result.append(readAll(QDir(directory).filePath(name)));
    return result;
}
}  // namespace

/** 使用临时目录和真实写线程，验证可复盘内容、容量、并发及失败降级；不写用户日志。 */
class SessionLogTest : public QObject {
    Q_OBJECT
   private slots:
    void savesStructuredMessagesAndCleanExit() {
        QTemporaryDir root;
        SessionLog log({.root_directory = root.path()});
        QVERIFY2(log.error().isEmpty(), qPrintable(log.error()));
        log.record("info", "video", "中文事件\n下一行", {{"fps", 60}});
        QVERIFY(log.finish(7));
        const auto bytes = events(log.sessionDirectory());
        QVERIFY(bytes.contains("session_started"));
        QVERIFY(bytes.contains("session_finished"));
        bool found = false;
        for (const auto& line : bytes.split('\n')) {
            if (line.isEmpty()) continue;
            QJsonParseError error;
            const auto object = QJsonDocument::fromJson(line, &error).object();
            QCOMPARE(error.error, QJsonParseError::NoError);
            if (object.value("category") == "video") {
                QCOMPARE(object.value("message").toString(), QString("中文事件\n下一行"));
                QCOMPARE(object.value("fields").toObject().value("fps").toInt(), 60);
                QVERIFY(object.contains("utc"));
                QVERIFY(object.contains("uptimeUs"));
                QVERIFY(object.contains("thread"));
                found = true;
            }
        }
        QVERIFY(found);
        const auto state =
            QJsonDocument::fromJson(readAll(QDir(log.sessionDirectory()).filePath("session.json")))
                .object();
        QCOMPARE(state.value("state").toString(), QString("finished"));
        QCOMPARE(state.value("exitCode").toInt(), 7);
    }

    void capturesQtMessagesAndRedactsCredentials() {
        QTemporaryDir root;
        SessionLog log({.root_directory = root.path()});
        QVERIFY(log.installQtHandler());
        qWarning("password=private-value Bearer private-bearer mqtt://user:pass@example.test/");
        qWarning(R"({"token":"json-private", "password": "json-password"})");
        log.record("info", "test", "token=private-token",
                   {{"password", "private-field"}, {"safe", "api_key=private-key"}});
        QVERIFY(log.flush());
        const auto bytes = events(log.sessionDirectory());
        QVERIFY(bytes.contains("redacted"));
        for (const auto* secret : {"private-value", "private-bearer", "user:pass", "private-token",
                                   "private-field", "private-key", "json-private", "json-password"})
            QVERIFY2(!bytes.contains(secret), secret);
        QVERIFY(log.finish(0));
    }

    void drainsConcurrentWritersWithoutLosingRecords() {
        QTemporaryDir root;
        SessionLog log({.root_directory = root.path(), .queue_records = 8192});
        std::vector<std::thread> workers;
        for (int thread = 0; thread < 4; ++thread)
            workers.emplace_back([&, thread] {
                for (int i = 0; i < 100; ++i)
                    log.record("info", "concurrent",
                               QString::number(thread) + ":" + QString::number(i));
            });
        for (auto& worker : workers) worker.join();
        QVERIFY(log.finish(0));
        QCOMPARE(events(log.sessionDirectory()).count("\"category\":\"concurrent\""), 400);
    }

    void rotatesFilesAndLeavesAbnormalSessionMarker() {
        QTemporaryDir root;
        QString directory;
        {
            SessionLog log({.root_directory = root.path(),
                            .file_bytes = 1024,
                            .files_per_session = 2,
                            .queue_records = 8192});
            directory = log.sessionDirectory();
            for (int i = 0; i < 100; ++i)
                log.record("info", "rotation", QString(200, 'x'), {{"index", i}});
            QVERIFY(log.flush());
        }
        const auto files = QDir(directory).entryInfoList({"events_*.jsonl"}, QDir::Files);
        QCOMPARE(files.size(), 2);
        for (const auto& file : files) QVERIFY(file.size() <= 1024);
        const auto bytes = events(directory);
        QVERIFY(bytes.contains("\"index\":99"));
        const auto state =
            QJsonDocument::fromJson(readAll(QDir(directory).filePath("session.json"))).object();
        QCOMPARE(state.value("state").toString(), QString("active"));
    }

    void prunesOldSessionsButPreservesActiveAndUnrelatedDirectories() {
        QTemporaryDir root;
        SessionLog active({.root_directory = root.path(), .retained_sessions = 2});
        const auto unrelated = QDir(root.path()).filePath("session_my_notes");
        QVERIFY(QDir().mkpath(unrelated));
        for (int i = 0; i < 4; ++i) {
            SessionLog old({.root_directory = root.path(), .retained_sessions = 2});
            QVERIFY(old.finish(0));
        }
        QVERIFY(QDir(active.sessionDirectory()).exists());
        QVERIFY(QDir(unrelated).exists());
        // 时间戳相同的会话按唯一后缀排序；较老活跃会话允许额外保留，但旧目录必须被裁剪。
        QVERIFY(
            QDir(root.path()).entryList({"session_*"}, QDir::Dirs | QDir::NoDotAndDotDot).size() <=
            4);
        // 活跃会话优先保留，允许超过常规保留数量；不会为删旧日志抢占其他进程文件锁。
        QVERIFY(active.finish(0));
    }

    void oversizedUnicodeRecordsRemainBoundedAndKeepTimeContext() {
        QTemporaryDir root;
        SessionLog log({.root_directory = root.path(), .file_bytes = 1024});
        log.record("warning", "unicode", QString(4096, QChar(0x4e2d)),
                   {{"large", QString(4096, QChar(0x6587))}});
        QVERIFY(log.flush());
        const auto files =
            QDir(log.sessionDirectory()).entryInfoList({"events_*.jsonl"}, QDir::Files);
        for (const auto& file : files) QVERIFY(file.size() <= 1024);
        const auto bytes = events(log.sessionDirectory());
        bool found = false;
        for (const auto& line : bytes.split('\n')) {
            const auto event = QJsonDocument::fromJson(line).object();
            if (event.value("category") != "unicode") continue;
            QVERIFY(event.value("fieldsOmitted").toBool());
            QVERIFY(event.contains("utc"));
            QVERIFY(event.contains("uptimeUs"));
            QVERIFY(event.contains("thread"));
            found = true;
        }
        QVERIFY(found);
        QVERIFY(log.finish(0));
    }

    void saturatedQueueReportsLossWithoutBreakingFileBudget() {
        QTemporaryDir root;
        SessionLog log({.root_directory = root.path(),
                        .file_bytes = 1024,
                        .files_per_session = 16,
                        .queue_records = 1});
        // 并发生产持续快于每条真实文件 flush，检验超额记录有明确计数且退出事件能排空。
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 4; ++worker)
            workers.emplace_back([&] {
                for (int i = 0; i < 2000; ++i) log.record("info", "overload", QString(700, 'x'));
            });
        for (auto& worker : workers) worker.join();
        QVERIFY(log.finish(0));
        const auto files =
            QDir(log.sessionDirectory()).entryInfoList({"events_*.jsonl"}, QDir::Files);
        for (const auto& file : files) QVERIFY(file.size() <= 1024);
        const auto bytes = events(log.sessionDirectory());
        QVERIFY(bytes.contains("droppedRecords"));
        QVERIFY(bytes.contains("session_finished"));
        for (const auto& line : bytes.split('\n')) {
            if (line.isEmpty()) continue;
            QJsonParseError error;
            QJsonDocument::fromJson(line, &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
        }
    }

    void failedDirectoryDoesNotPreventApplicationFromContinuing() {
        QTemporaryDir root;
        QFile blocker(QDir(root.path()).filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        SessionLog log({.root_directory = blocker.fileName() + "/logs"});
        QVERIFY(!log.error().isEmpty());
        QVERIFY(!log.installQtHandler());
        log.record("error", "test", "still alive");
        QVERIFY(!log.flush(1));
        QVERIFY(!log.finish(0));
    }
};

QTEST_GUILESS_MAIN(SessionLogTest)
#include "session_log_test.moc"
