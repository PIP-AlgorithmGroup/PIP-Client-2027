#include "app/update_release.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

/** 版本/发行选择不联网；同时验证预发布顺序、缺包和发布来源边界。 */
class UpdateReleaseTest : public QObject {
    Q_OBJECT
   private slots:
    void semanticOrder() {
        const QStringList versions{"0.4.0-alpha.2", "0.4.0-alpha.10", "0.4.0-beta.1",
                                   "0.4.0-rc",      "0.4.0-rc.1",     "0.4.0-rc.10",
                                   "0.4.0",         "0.10.0",         "1.0.0"};
        for (qsizetype i = 1; i < versions.size(); ++i)
            QCOMPARE(compareUpdateVersions(versions[i - 1], versions[i]), std::optional<int>(-1));
        QCOMPARE(compareUpdateVersions("v0.4.0-rc.1+build.2", "0.4.0-rc.1+build.1"),
                 std::optional<int>(0));
        for (const auto* invalid : {"0.4", "0.04.0", "0.4.0-rc.01", "0.4.0-", "garbage"})
            QVERIFY(!compareUpdateVersions(invalid, "0.4.0"));
    }

    void selectChannelAndVerifyAsset() {
        const auto digest = QString(64, 'a');
        const auto make_release = [&](const QString& version, bool prerelease) {
            const auto file = "PIPClient-" + version + "-Windows-x86_64-Setup.exe";
            return QJsonObject{
                {"tag_name", "v" + version},
                {"draft", false},
                {"prerelease", prerelease},
                {"body", "Release notes"},
                {"html_url",
                 "https://github.com/PIP-AlgorithmGroup/PIP-Client-2027/releases/tag/v" + version},
                {"assets",
                 QJsonArray{QJsonObject{
                     {"name", file},
                     {"size", 123},
                     {"state", "uploaded"},
                     {"digest", "sha256:" + digest},
                     {"browser_download_url",
                      "https://github.com/PIP-AlgorithmGroup/PIP-Client-2027/releases/download/v" +
                          version + "/" + file}}}}};
        };
        QJsonArray releases{make_release("0.5.0-beta.1", true), make_release("0.4.0", false)};
        auto result =
            selectUpdateRelease(releases, "0.4.0-rc.1", false, "Windows-x86_64-Setup.exe");
        QCOMPARE(result.release.version, QString("0.4.0"));
        QCOMPARE(result.release.sha256, QByteArray(32, char(0xaa)));
        result = selectUpdateRelease(releases, "0.4.0-rc.1", true, "Windows-x86_64-Setup.exe");
        QCOMPARE(result.release.version, QString("0.5.0-beta.1"));
        QVERIFY(
            !selectUpdateRelease(releases, "0.5.0", true, "Windows-x86_64-Setup.exe").has_update);
        QVERIFY(
            !selectUpdateRelease(releases, "0.4.0-rc.1", true, "Ubuntu-amd64.deb").error.isEmpty());
        auto broken = releases[0].toObject();
        auto asset = broken["assets"].toArray()[0].toObject();
        asset["digest"] = "sha256:invalid";
        broken["assets"] = QJsonArray{asset};
        QVERIFY(!selectUpdateRelease(QJsonArray{broken}, "0.4.0", true, "Windows-x86_64-Setup.exe")
                     .error.isEmpty());
        asset["digest"] = "sha256:" + digest;
        asset["browser_download_url"] = "https://example.com/installer.exe";
        broken["assets"] = QJsonArray{asset};
        QVERIFY(!selectUpdateRelease(QJsonArray{broken}, "0.4.0", true, "Windows-x86_64-Setup.exe")
                     .error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(UpdateReleaseTest)
#include "update_release_test.moc"
