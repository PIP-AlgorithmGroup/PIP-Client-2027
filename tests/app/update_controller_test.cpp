#include "app/update_controller.h"

#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {
/** 真正的环回 HTTP fixture，不伪造 QNetworkReply；每个测试使用独立端口和缓存。 */
class ReleaseServer : public QTcpServer {
   public:
    QByteArray package = "fixture installer bytes";
    QByteArray downloaded = package;
    int requests = 0;
    bool stall_package = false, fail_metadata = false, redirect_external = false;
    explicit ReleaseServer(QObject* parent = nullptr) : QTcpServer(parent) {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    auto input = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", input);
                    if (!input.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                    socket->setProperty("handled", true);
                    ++requests;
                    const bool metadata = input.startsWith("GET /repos/");
                    if (!metadata && stall_package) return;
                    if (metadata && fail_metadata) {
                        socket->write("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n");
                    } else if (!metadata && redirect_external) {
                        socket->write(
                            "HTTP/1.1 302 Found\r\nLocation: "
                            "https://example.com/evil.exe\r\nContent-Length: 0\r\n\r\n");
                    } else {
                        const auto body =
                            metadata ? QJsonDocument(releases()).toJson() : downloaded;
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                                      QByteArray::number(body.size()) +
                                      "\r\nConnection: close\r\n\r\n" + body);
                    }
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QJsonArray releases() const {
        const QString version = "0.5.0-rc.2";
        const QString file = "PIPClient-" + version + "-Windows-x86_64-Setup.exe";
        const auto digest = QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex();
        return QJsonArray{QJsonObject{
            {"tag_name", "v" + version},
            {"draft", false},
            {"prerelease", true},
            {"body", "Fixture release notes"},
            {"assets",
             QJsonArray{QJsonObject{
                 {"name", file},
                 {"state", "uploaded"},
                 {"size", package.size()},
                 {"digest", "sha256:" + QString::fromLatin1(digest)},
                 {"browser_download_url",
                  "https://github.com/PIP-AlgorithmGroup/PIP-Client-2027/releases/download/v" +
                      version + "/" + file}}}}}};
    }
    UpdateController::Options options(const QString& directory) const {
        return {.current_version = "0.4.0-rc.1",
                .settings_path = directory + "/updates.ini",
                .cache_directory = directory + "/cache",
                .platform_suffix = "Windows-x86_64-Setup.exe",
                .test_origin = QUrl("http://127.0.0.1:" + QString::number(serverPort())),
                .timeout_ms = 300};
    }
};
}  // namespace

/** 覆盖真实异步网络、文件落盘、摘要、取消、渠道及安装资格；从不执行系统安装器。 */
class UpdateControllerTest : public QObject {
    Q_OBJECT
   private slots:
    void manualCheckDownloadAndUnsupportedInstall() {
        QTemporaryDir directory;
        ReleaseServer server;
        UpdateController controller(server.options(directory.path()));
        QCOMPARE(server.requests, 0);
        QCOMPARE(controller.state(), QString("idle"));
        QVERIFY(controller.includePrerelease());
        controller.check();
        controller.check();
        QTRY_COMPARE(controller.state(), QString("available"));
        QCOMPARE(server.requests, 1);
        QCOMPARE(controller.availableVersion(), QString("0.5.0-rc.2"));
        QCOMPARE(controller.releaseNotes(), QString("Fixture release notes"));
        controller.download();
        QTRY_COMPARE(controller.state(), QString("ready"));
        QCOMPARE(controller.progress(), 1.0);
        QDirIterator files(directory.path(), {"*.exe"}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY(files.hasNext());
        QFile package(files.next());
        QVERIFY(package.open(QIODevice::ReadOnly));
        QCOMPARE(package.readAll(), server.package);
        QSignalSpy quitting(&controller, &UpdateController::quitForUpdate);
        QVERIFY(!controller.canInstall());
        controller.install();
        QCOMPARE(quitting.count(), 0);
        controller.setInstallAllowed(false);
        QVERIFY(controller.installHint().contains("欢迎页"));
    }
    void corruptAndOversizedPackagesNeverBecomeReady_data() {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("wrong-digest") << QByteArray("fixture installer bytex");
        QTest::newRow("oversize") << QByteArray(100, 'x');
    }
    void corruptAndOversizedPackagesNeverBecomeReady() {
        QFETCH(QByteArray, content);
        QTemporaryDir directory;
        ReleaseServer server;
        server.downloaded = content;
        UpdateController controller(server.options(directory.path()));
        controller.check();
        QTRY_COMPARE(controller.state(), QString("available"));
        controller.download();
        QTRY_COMPARE(controller.state(), QString("error"));
        QDirIterator files(directory.path(), {"*.exe"}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY(!files.hasNext());
        QVERIFY(!controller.canInstall());
        server.downloaded = server.package;
        controller.download();
        QTRY_COMPARE(controller.state(), QString("ready"));
    }
    void cancelTimeoutAndRetry() {
        QTemporaryDir directory;
        ReleaseServer server;
        UpdateController controller(server.options(directory.path()));
        controller.check();
        QTRY_COMPARE(controller.state(), QString("available"));
        server.stall_package = true;
        controller.download();
        QTRY_COMPARE(server.requests, 2);
        controller.cancel();
        QCOMPARE(controller.state(), QString("idle"));
        controller.download();
        QTRY_COMPARE(controller.state(), QString("error"));
        server.stall_package = false;
        controller.download();
        QTRY_COMPARE(controller.state(), QString("ready"));
    }
    void networkFailureIsNotCurrentAndRedirectIsRejected() {
        QTemporaryDir directory;
        ReleaseServer server;
        server.fail_metadata = true;
        UpdateController controller(server.options(directory.path()));
        controller.check();
        QTRY_COMPARE(controller.state(), QString("error"));
        server.fail_metadata = false;
        controller.check();
        QTRY_COMPARE(controller.state(), QString("available"));
        server.redirect_external = true;
        controller.download();
        QTRY_COMPARE(controller.state(), QString("error"));
        QVERIFY(controller.status().contains("不受信任"));
    }
    void channelPersistsAndClearsCandidate() {
        QTemporaryDir directory;
        ReleaseServer server;
        {
            UpdateController controller(server.options(directory.path()));
            controller.check();
            QTRY_COMPARE(controller.state(), QString("available"));
            controller.setIncludePrerelease(false);
            QVERIFY(controller.availableVersion().isEmpty());
            controller.check();
            QTRY_COMPARE(controller.state(), QString("current"));
        }
        UpdateController restored(server.options(directory.path()));
        QVERIFY(!restored.includePrerelease());
    }
    void liveGitHubCheckAndDownload() {
        if (!qEnvironmentVariableIsSet("PIP_UPDATE_LIVE_TEST"))
            QSKIP("显式启用真实 GitHub 下载验证");
        QTemporaryDir directory;
        UpdateController controller({.current_version = "0.0.0-rc.1",
                                     .settings_path = directory.filePath("updates.ini"),
                                     .cache_directory = directory.filePath("cache")});
        controller.check();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 65000);
        QCOMPARE(controller.state(), QString("available"));
        controller.download();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 180000);
        QCOMPARE(controller.state(), QString("ready"));
        qInfo() << "Verified official release" << controller.availableVersion();
    }
};
QTEST_GUILESS_MAIN(UpdateControllerTest)
#include "update_controller_test.moc"
