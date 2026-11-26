#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <QUrl>
#include <optional>

/** 经校验的 GitHub 发行附件；size 单位为字节，sha256 为 32 字节原始摘要。 */
struct UpdateRelease {
    QString version, notes, file_name;
    QUrl page_url, download_url;
    qint64 size = 0;
    QByteArray sha256;
};

/** 选择结果：无新版时 has_update=false；发现新版但缺少可信附件时返回 error。 */
struct UpdateSelection {
    bool has_update = false;
    UpdateRelease release;
    QString error;
};

/** 严格按 SemVer 比较，忽略可选 v 前缀及构建元数据；非法版本返回 nullopt。无 I/O/线程状态。 */
std::optional<int> compareUpdateVersions(const QString& left, const QString& right);

/** 从已分页收集的发行列表选择最高版本，跳过草稿和渠道外预发布。
 * 只接受官方仓库、固定平台包名、已上传状态与 SHA-256 摘要；不跳过最新版本的缺包错误。
 * platform_suffix 为受信任的构建平台常量；无 I/O，允许在任意线程独立调用。
 */
UpdateSelection selectUpdateRelease(const QJsonArray& releases, const QString& current,
                                    bool include_prerelease, const QString& platform_suffix);
