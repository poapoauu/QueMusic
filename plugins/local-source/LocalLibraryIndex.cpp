#include "LocalLibraryIndex.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <utility>

namespace {
SourceErrorV2 watchFailure()
{
    SourceErrorV2 error;
    error.kind = SourceErrorKindV2::Unavailable;
    error.messageKey = "local.watch.incomplete";
    return error;
}

QByteArray fingerprint(const SourceConfigurationV2 &source, const LocalScanConfig &scan)
{
    QVariantMap parameters = source.parameters;
    parameters.insert("rootDirectory", scan.canonicalRoot);
    parameters.insert("recursive", scan.recursive);
    QStringList ignored = scan.ignoreDirectories;
    ignored.sort();
    parameters.insert("ignoreDirectories", ignored.join('\n'));
    parameters.insert("scanOnOpen", source.parameters.value("scanOnOpen", true).toBool());
    parameters.insert("watchChanges", source.parameters.value("watchChanges", false).toBool());
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << source.pluginPackageId << source.sourceId << source.sourceInstanceId
           << source.accountId << source.displayName << parameters << source.secret;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool registerPath(QFileSystemWatcher &watcher, const QString &path,
                  const LocalLibraryIndex::Hooks &hooks)
{
    return hooks.registerWatch ? hooks.registerWatch(watcher, path) : watcher.addPath(path);
}
}

LocalLibraryIndex::LocalLibraryIndex(QString instanceId, QByteArray configurationFingerprint,
                                     LocalScanConfig config, bool watchChanges, QObject *parent,
                                     Hooks hooks)
    : QObject(parent), m_instanceId(std::move(instanceId)),
      m_fingerprint(std::move(configurationFingerprint)), m_config(std::move(config)),
      m_watchChanges(watchChanges), m_hooks(std::move(hooks)), m_watcher(this), m_debounce(this)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (!m_stopped) m_debounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        if (!m_stopped) m_debounce.start();
    });
    connect(&m_debounce, &QTimer::timeout, this, &LocalLibraryIndex::requestScan);
}

LocalLibraryIndex::~LocalLibraryIndex()
{
    stop();
}

void LocalLibraryIndex::requestScan()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopped) return;
    ++m_generation;
    if (m_watchChanges) {
        const QString parent = QFileInfo(m_config.canonicalRoot).absolutePath();
        if (!m_watcher.directories().contains(parent)
            && !registerPath(m_watcher, parent, m_hooks)) {
            m_cancel.store(true, std::memory_order_relaxed);
            m_pending = false;
            emit refreshFailed(watchFailure());
            return;
        }
    }
    if (m_running) {
        m_pending = true;
        m_cancel.store(true, std::memory_order_relaxed);
        return;
    }
    startWorker(m_generation);
}

void LocalLibraryIndex::startWorker(quint64 generation)
{
    Q_ASSERT(!m_worker.joinable());
    m_running = true;
    m_cancel.store(false, std::memory_order_relaxed);
    const LocalScanConfig config = m_config;
    const auto scan = m_hooks.scan;
    m_worker = std::thread([this, generation, config, scan] {
        LocalScanResult result = scan ? scan(config, m_cancel)
                                      : LocalSourceScanner().scan(config, m_cancel);
        QMetaObject::invokeMethod(this,
            [this, generation, result = std::move(result)]() mutable {
                finishWorker(generation, std::move(result));
            }, Qt::QueuedConnection);
    });
}

bool LocalLibraryIndex::registerWatchPaths(const LocalScanResult &scan)
{
    if (!m_watchChanges) return true;
    // Re-register every discovered directory, not merely the ones already in
    // QFileSystemWatcher: a deleted/replaced path may silently lose its watch.
    QStringList previous = m_watcher.directories();
    previous.append(m_watcher.files());
    if (!previous.isEmpty()) m_watcher.removePaths(previous);
    const QString parent = QFileInfo(m_config.canonicalRoot).absolutePath();
    QStringList wanted{parent};
    wanted.append(scan.watchedDirectories);
    for (const auto &entry : scan.entries) wanted.append(entry.canonicalPath);
    wanted.removeDuplicates();
    bool complete = true;
    for (const QString &path : wanted)
        if (!registerPath(m_watcher, path, m_hooks)) complete = false;
    return complete;
}

void LocalLibraryIndex::finishWorker(quint64 generation, LocalScanResult result)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_worker.joinable()) m_worker.join();
    m_running = false;
    if (m_stopped) return;

    if (generation == m_generation && !result.cancelled) {
        if (result.error) {
            emit refreshFailed(*result.error);
        } else if (!registerWatchPaths(result)) {
            emit refreshFailed(watchFailure());
        } else {
            auto next = std::make_shared<LocalIndexSnapshot>();
            next->revision = ++m_revision;
            next->generation = generation;
            next->scan = std::move(result);
            std::shared_ptr<const LocalIndexSnapshot> published = std::move(next);
            std::atomic_store(&m_snapshot, std::move(published));
            emit snapshotChanged(m_revision);
        }
    }
    if (m_pending && !m_stopped) {
        m_pending = false;
        startWorker(m_generation);
    }
}

std::shared_ptr<const LocalIndexSnapshot> LocalLibraryIndex::snapshot() const
{
    return std::atomic_load(&m_snapshot);
}

void LocalLibraryIndex::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopped) return;
    m_stopped = true;
    ++m_generation;
    m_cancel.store(true, std::memory_order_relaxed);
    m_pending = false;
    m_debounce.stop();
    if (m_worker.joinable()) m_worker.join();
    m_running = false;
    QStringList paths = m_watcher.directories();
    paths.append(m_watcher.files());
    if (!paths.isEmpty()) m_watcher.removePaths(paths);
}

LocalLibraryIndexPool::LocalLibraryIndexPool(LocalLibraryIndex::Hooks hooks)
    : m_hooks(std::move(hooks)) {}

LocalLibraryIndexPool::~LocalLibraryIndexPool()
{
    for (auto it = m_indexes.begin(); it != m_indexes.end(); ++it)
        if (auto index = it.value().lock()) index->stop();
}

std::shared_ptr<LocalLibraryIndex> LocalLibraryIndexPool::acquire(
    const SourceConfigurationV2 &configuration, const LocalScanConfig &scanConfig)
{
    const QByteArray key = fingerprint(configuration, scanConfig);
    if (auto existing = m_indexes.value(key).lock()) return existing;
    const bool watch = configuration.parameters.value("watchChanges", false).toBool();
    auto *index = new LocalLibraryIndex(configuration.sourceInstanceId, key, scanConfig,
                                        watch, nullptr, m_hooks);
    std::shared_ptr<LocalLibraryIndex> shared(index, [](LocalLibraryIndex *owned) {
        if (QThread::currentThread() == owned->thread()) {
            owned->stop();
            delete owned;
        } else {
            QMetaObject::invokeMethod(owned, [owned] {
                owned->stop();
                delete owned;
            }, Qt::BlockingQueuedConnection);
        }
    });
    m_indexes.insert(key, shared);
    return shared;
}
