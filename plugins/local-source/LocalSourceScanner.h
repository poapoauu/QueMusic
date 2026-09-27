#pragma once
#include "v2/SourceV2Types.h"
#include <atomic>
#include <functional>

struct LocalScanConfig {
    QString canonicalRoot;
    bool recursive = true;
    QStringList ignoreDirectories;
};
struct LocalScanEntry {
    QString entityId;
    MediaItemV2 item;
    QString canonicalPath;
};
struct LocalScanResult {
    QList<LocalScanEntry> entries;
    QStringList watchedDirectories;
    QStringList warningKeys;
    std::optional<SourceErrorV2> error;
    bool cancelled = false;
};

class LocalSourceScanner
{
public:
    // Optional access policy may deny additional paths, never grant access that
    // fails normal filesystem checks. Value-owned and called on the scan thread.
    using AccessPolicy = std::function<bool(const QString &, bool directory)>;
    explicit LocalSourceScanner(AccessPolicy access = {});
    static std::optional<LocalScanConfig> parseConfig(const QVariantMap &, SourceErrorV2 *);
    static std::optional<QString> validatedPath(const LocalScanConfig &, const QString &entityId,
                                                 bool directory);
    LocalScanResult scan(const LocalScanConfig &, const std::atomic_bool &) const;
private:
    AccessPolicy m_access;
};
