#include "update_release.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace {
struct Version {
    QStringList core, prerelease;
};

std::optional<Version> parseVersion(QString text) {
    static const QRegularExpression pattern(
        R"(^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$)");
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return {};
    Version result{{match.captured(1), match.captured(2), match.captured(3)}, {}};
    if (!match.captured(4).isEmpty()) result.prerelease = match.captured(4).split('.');
    static const QRegularExpression leading_zero("^0[0-9]+$");
    for (const auto& part : result.prerelease)
        if (leading_zero.match(part).hasMatch()) return {};
    return result;
}

// 按位数比较任意长度非负整数，不把远端版本号转换为可能溢出的机器整数。
int compareNumber(const QString& left, const QString& right) {
    if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
    const auto value = QString::compare(left, right, Qt::CaseSensitive);
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}
}  // namespace

std::optional<int> compareUpdateVersions(const QString& left, const QString& right) {
    const auto a = parseVersion(left), b = parseVersion(right);
    if (!a || !b) return {};
    for (int i = 0; i < 3; ++i) {
        const auto order = compareNumber(a->core[i], b->core[i]);
        if (order) return order;
    }
    if (a->prerelease.isEmpty() || b->prerelease.isEmpty())
        return a->prerelease.isEmpty() == b->prerelease.isEmpty() ? 0
               : a->prerelease.isEmpty()                          ? 1
                                                                  : -1;
    static const QRegularExpression numeric("^[0-9]+$");
    for (qsizetype i = 0; i < std::min(a->prerelease.size(), b->prerelease.size()); ++i) {
        const auto& x = a->prerelease[i];
        const auto& y = b->prerelease[i];
        const bool x_numeric = numeric.match(x).hasMatch(), y_numeric = numeric.match(y).hasMatch();
        const int order = x_numeric && y_numeric   ? compareNumber(x, y)
                          : x_numeric != y_numeric ? (x_numeric ? -1 : 1)
                                                   : QString::compare(x, y, Qt::CaseSensitive);
        if (order) return order < 0 ? -1 : 1;
    }
    return a->prerelease.size() == b->prerelease.size()  ? 0
           : a->prerelease.size() < b->prerelease.size() ? -1
                                                         : 1;
}

UpdateSelection selectUpdateRelease(const QJsonArray& releases, const QString& current,
                                    bool include_prerelease, const QString& platform_suffix) {
    UpdateSelection result;
    if (!parseVersion(current)) {
        result.error = "当前程序版本格式无效，无法检查更新。";
        return result;
    }
    QJsonObject selected;
    QString selected_version = current;
    for (const auto& value : releases) {
        const auto release = value.toObject();
        const auto tag = release["tag_name"].toString();
        const auto parsed = parseVersion(tag);
        if (release["draft"].toBool() || !parsed ||
            (!include_prerelease &&
             (release["prerelease"].toBool() || !parsed->prerelease.isEmpty())))
            continue;
        if (compareUpdateVersions(tag, selected_version).value_or(0) > 0) {
            selected = release;
            selected_version = tag;
        }
    }
    if (selected.isEmpty()) return result;
    if (selected_version.startsWith('v')) selected_version.remove(0, 1);
    auto& release = result.release;
    release.version = selected_version;
    release.notes = selected["body"].toString().left(16000);
    const QString repository = "https://github.com/PIP-AlgorithmGroup/PIP-Client-2027";
    release.page_url = QUrl(repository + "/releases/tag/" + selected["tag_name"].toString());
    release.file_name = "PIPClient-" + selected_version + "-" + platform_suffix;
    for (const auto& value : selected["assets"].toArray()) {
        const auto asset = value.toObject();
        if (asset["name"].toString() != release.file_name) continue;
        const auto digest = asset["digest"].toString();
        static const QRegularExpression sha256("^sha256:[0-9a-fA-F]{64}$");
        const auto url = asset["browser_download_url"].toString();
        const auto expected = repository + "/releases/download/" + selected["tag_name"].toString() +
                              "/" + release.file_name;
        const auto size = asset["size"].toDouble(-1);
        // 元数据与下载均有上限；不得把异常大小或缺失摘要降级成可安装附件。
        if (asset["state"].toString() != "uploaded" || !sha256.match(digest).hasMatch() ||
            url != expected || size <= 0 || size > 2LL * 1024 * 1024 * 1024 ||
            size != qint64(size)) {
            result.error = "新版安装包元数据或 SHA-256 不完整，请稍后重试或前往发行页。";
            return result;
        }
        release.size = qint64(size);
        release.sha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
        release.download_url = QUrl(url);
        result.has_update = true;
        return result;
    }
    result.error = "发现新版，但该版本尚未提供本平台安装包。";
    return result;
}
