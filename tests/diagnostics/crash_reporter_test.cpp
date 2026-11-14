#include "platform/crash_reporter.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

/** 原生崩溃测试仅终止 helper 子进程；测试主进程保留断言、临时目录与结果文本。 */
class CrashReporterTest : public QObject {
    Q_OBJECT

   private slots:
    void invalidDirectoryAndExistingEvidenceAreRejected() {
        QTemporaryDir directory;
        CrashReporter reporter;
        QString error;
        QVERIFY(!reporter.install({}, &error));
        QVERIFY(!error.isEmpty());
        QFile evidence(directory.filePath("crash.txt"));
        QVERIFY(evidence.open(QIODevice::WriteOnly));
        QCOMPARE(evidence.write("preserve"), 8);
        evidence.close();
        QVERIFY(!reporter.install(directory.path(), &error));
        QVERIFY(evidence.open(QIODevice::ReadOnly));
        QCOMPARE(evidence.readAll(), QByteArray("preserve"));
    }

    void normalExitRestoresHandlersAndRemovesPlaceholders_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("normal") << QString("normal");
        QTest::newRow("duplicate") << QString("duplicate");
        QTest::newRow("replacement") << QString("restore");
    }
    void normalExitRestoresHandlersAndRemovesPlaceholders() {
        QFETCH(QString, mode);
        QTemporaryDir directory;
        QProcess process;
        process.setWorkingDirectory(directory.path());
        process.start(QString(PIP_CRASH_HELPER_PATH), {directory.path(), mode});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(!QFile::exists(directory.filePath("crash.txt")));
        QVERIFY(!QFile::exists(directory.filePath("crash.dmp")));
    }

    void nativeFatalErrorsLeaveEvidence_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("abort") << QString("abort");
        QTest::newRow("terminate") << QString("terminate");
        QTest::newRow("qt-fatal") << QString("fatal");
        QTest::newRow("access-violation") << QString("access");
        QTest::newRow("worker-abort") << QString("worker-abort");
        QTest::newRow("worker-terminate") << QString("worker-terminate");
        QTest::newRow("worker-access") << QString("worker-access");
    }
    void nativeFatalErrorsLeaveEvidence() {
        QFETCH(QString, mode);
        QTemporaryDir directory;
        QProcess process;
        process.setWorkingDirectory(directory.path());
        process.start(QString(PIP_CRASH_HELPER_PATH), {directory.path(), mode});
        QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForFinished(30000), qPrintable(process.errorString()));
        QVERIFY(process.exitStatus() == QProcess::CrashExit || process.exitCode() != 0);
        QDir evidence_directory(directory.path());
        if (mode == "fatal") {
            const auto sessions = evidence_directory.entryList({"session_*"}, QDir::Dirs);
            QCOMPARE(sessions.size(), 1);
            QVERIFY(evidence_directory.cd(sessions.front()));
            QFile events(evidence_directory.filePath("events_0.jsonl"));
            QVERIFY(events.open(QIODevice::ReadOnly));
            QVERIFY(events.readAll().contains("crash reporter helper fatal message"));
        }
        QFile marker(evidence_directory.filePath("crash.txt"));
        QVERIFY(marker.open(QIODevice::ReadOnly));
        const auto evidence = marker.readAll();
#ifdef Q_OS_WIN
        QVERIFY2(evidence.contains("reason="), evidence.constData());
        QVERIFY2(evidence.contains("minidump=written"), evidence.constData());
        QFile dump(evidence_directory.filePath("crash.dmp"));
        QVERIFY(dump.open(QIODevice::ReadOnly));
        QVERIFY(dump.size() > 32);
        QCOMPARE(dump.read(4), QByteArray("MDMP"));
#else
        QVERIFY2(evidence.contains("fatal_signal="), evidence.constData());
        if (mode.endsWith("terminate")) QVERIFY(evidence.contains("reason=terminate"));
#endif
    }
};

QTEST_GUILESS_MAIN(CrashReporterTest)
#include "crash_reporter_test.moc"
