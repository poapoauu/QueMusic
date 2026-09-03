#pragma once
#include "v2/SourceV2Types.h"
#include <chrono>
class QThreadPool;

struct PageCacheKeyV2 {
    PageQueryV2 query;
    QStringList sourceInstanceIds;
};
struct CachedPageV2 {
    PageResultV2 page;
    bool cached = true;
    bool stale = false;
    QDateTime storedAt;
};

// Synchronous low-level file API. Repositories use the shared serial IO pool;
// copying a cache copies its directory configuration, not a QObject/file handle.
class PageCache final {
public:
    explicit PageCache(QString directory = {});
    std::optional<CachedPageV2> lookup(const PageCacheKeyV2 &key, QDateTime now,
                                       std::chrono::seconds maxAge) const;
    bool store(const PageCacheKeyV2 &key, const PageResultV2 &page, QDateTime storedAt);
    void invalidateSource(const QString &sourceInstanceId);
    QString filePath(const PageCacheKeyV2 &key) const;
    static QString queryScope(const PageCacheKeyV2 &key);
    static PageResultV2 sanitized(const PageResultV2 &page);
private:
    QString m_directory;
};

// One process-wide FIFO executor orders cache store/lookup/invalidation without
// waiting for disk on repository destruction. No network runs in the host.
QThreadPool *musicCacheIoPool();
