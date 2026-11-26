#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

/** 通过独立父进程、伪安装器与客户端验证真实等待/退出码/重启，不触发系统安装。 */
class UpdateHelperTest : public QObject {
    Q_OBJECT
   private slots:
    void waitsForParentAndRestarts_data() {
        QTest::addColumn<int>("installer_exit");
        QTest::newRow("success") << 0;
        QTest::newRow("cancelled") << 1;
    }
    void waitsForParentAndRestarts() {
        QFETCH(int, installer_exit);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString suffix =
#ifdef Q_OS_WIN
            ".exe";
#else
            "";
#endif
        const auto fixture = QString::fromUtf8(PIP_UPDATE_FIXTURE);
        const auto installer = directory.filePath("installer with spaces" + suffix);
        const auto client = directory.filePath("client with spaces" + suffix);
        const auto result_path = directory.filePath("result.txt");
        QVERIFY(QFile::copy(fixture, installer));
        QVERIFY(QFile::copy(fixture, client));
        QFile::setPermissions(
            installer, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile::setPermissions(
            client, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile exit_file(installer + ".exit");
        QVERIFY(exit_file.open(QIODevice::WriteOnly));
        exit_file.write(QByteArray::number(installer_exit));
        exit_file.close();
        QProcess parent;
        parent.start(fixture, {"--parent", QString::fromUtf8(PIP_UPDATE_TEST_HELPER), installer,
                               client, result_path});
        QVERIFY(parent.waitForStarted());
        QTest::qWait(200);
        QVERIFY(!QFile::exists(installer + ".ran"));
        QVERIFY(parent.waitForFinished(10000));
        QCOMPARE(parent.exitCode(), 0);
        QTRY_VERIFY(QFile::exists(client + ".ran"));
        QVERIFY(QFile::exists(installer + ".ran"));
        QFile result(result_path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll().trimmed(),
                 installer_exit == 0 ? QByteArray("success") : QByteArray("failed-or-cancelled"));
        // 启动标记可能在退出前写入；确保临时 EXE 不再被新进程占用后再清理目录。
        QTRY_VERIFY(!QFile::exists(client) || QFile::remove(client));
        QTRY_VERIFY(!QFile::exists(installer) || QFile::remove(installer));
    }
    void rejectsInvalidParentWithoutInstalling() {
        QProcess helper;
        helper.start(QString::fromUtf8(PIP_UPDATE_TEST_HELPER),
                     {"0", "/invalid", "/invalid", "/invalid"});
        QVERIFY(helper.waitForFinished());
        QCOMPARE(helper.exitCode(), 2);
    }
};
QTEST_GUILESS_MAIN(UpdateHelperTest)
#include "update_helper_test.moc"
