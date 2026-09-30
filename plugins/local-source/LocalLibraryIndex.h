#pragma once

#include "LocalSourceScanner.h"
#include "LocalIdentityStore.h"
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

struct LocalIndexSnapshot {
    quint64 revision = 0;
    quint64 generation = 0;
    LocalScanResult scan;
    QHash<QString, QString> trackPathById;
    QHash<QString, QString> directoryPathById;
    QHash<QString, QString> trackIdByPath;
    QHash<QString, QString> directoryIdByPath;
};

// Plugin-owned and deliberately independent of Host models and the global
// thread pool. All mutating methods/signals run on the QObject owner thread.
class LocalLibraryIndex final : public QObject
{
    Q_OBJECT
public:
    struct Hooks {
        std::function<LocalScanResult(const LocalScanConfig &, const std::atomic_bool &)> scan;
        std::function<bool(QFileSystemWatcher &, const QString &)> registerWatch;
    };

    LocalLibraryIndex(QString instanceId, QByteArray configurationFingerprint,
                      LocalScanConfig config, bool watchChanges, QObject *parent = nullptr,
                      Hooks hooks = {}, QString identityFile = {});
    ~LocalLibraryIndex() override;

    void requestScan();
    void stop();
    std::shared_ptr<const LocalIndexSnapshot> snapshot() const;
    bool persistedIdentities(LocalIdentitySnapshot *out) const;

signals:
    void snapshotChanged(quint64 revision);
    void refreshFailed(SourceErrorV2 error);

private:
    void startWorker(quint64 generation);
    void finishWorker(quint64 generation, LocalScanResult result);
    bool registerWatchPaths(const LocalScanResult &scan);

    QString m_instanceId;
    LocalScanConfig m_config;
    bool m_watchChanges = false;
    Hooks m_hooks;
    QString m_identityFile;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    std::thread m_worker;
    std::atomic_bool m_cancel{false};
    quint64 m_generation = 0;
    quint64 m_revision = 0;
    bool m_running = false;
    bool m_pending = false;
    bool m_stopped = false;
    std::shared_ptr<const LocalIndexSnapshot> m_snapshot;
};

// Stored as a Plugin member; each Session holds a shared lease. The map is
// weak so the final Session release synchronously stops/joins its index.
class LocalLibraryIndexPool final
{
public:
    explicit LocalLibraryIndexPool(LocalLibraryIndex::Hooks hooks = {}, QString identityRoot = {});
    ~LocalLibraryIndexPool();
    std::shared_ptr<LocalLibraryIndex> acquire(const SourceConfigurationV2 &configuration,
                                               const LocalScanConfig &scanConfig);
private:
    LocalLibraryIndex::Hooks m_hooks;
    QString m_identityRoot;
    QHash<QByteArray, std::weak_ptr<LocalLibraryIndex>> m_indexes;
};
