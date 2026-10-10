#include "QueueHistoryStore.h"

#include "PlaybackCoordinator.h"
#include "QueueHistoryCodec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace {
constexpr qint64 maxBytes = 16 * 1024 * 1024;

int matchingQueueIndex(const RecentPlay &play, const QList<QueueOccurrence> &trusted,
                       const QVariantList &queue, bool requireAvailable = true)
{
    for (int index = 0; index < trusted.size() && index < queue.size(); ++index) {
        if (trusted.at(index).occurrenceId == play.occurrenceId
            && trusted.at(index).ref == play.ref
            && (!requireAvailable
                || !queue.at(index).toMap().value(QStringLiteral("unavailable")).toBool())) return index;
    }
    return -1;
}

bool atomicWrite(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

bool protectedCopy(const QString &source, const QString &destination)
{
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return false;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return false;
    QFile output(destination);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    bool ok = output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    while (ok && !input.atEnd()) {
        const QByteArray chunk = input.read(64 * 1024);
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) ok = false;
    }
    if (ok) ok = output.flush();
    output.close();
    if (!ok) output.remove(); // Only the destination created above.
    return ok;
}
}

QueueHistoryStore::QueueHistoryStore(PlaybackCoordinator *coordinator, QString filePath,
                                     QObject *parent, Hooks hooks)
    : QObject(parent), m_coordinator(coordinator), m_filePath(std::move(filePath)),
      m_hooks(std::move(hooks))
{
    if (!m_hooks.write) m_hooks.write = atomicWrite;
    if (!m_hooks.backup) m_hooks.backup = protectedCopy;
    if (m_coordinator) connect(m_coordinator, &QObject::destroyed, this, [this] {
        m_coordinator = nullptr;
        emit historyChanged();
    });
}

QString QueueHistoryStore::warningKey() const { return m_warningKey; }
QString QueueHistoryStore::backupPath() const { return m_backupPath; }
QList<RecentPlay> QueueHistoryStore::history() const { return m_history; }

int QueueHistoryStore::latestQueueIndex() const
{
    return m_history.isEmpty() ? -1 : queueIndex(m_history.first());
}

int QueueHistoryStore::queueIndex(const RecentPlay &play) const
{
    if (!m_coordinator) return -1;
    // An occurrence UUID alone is not authority: a restored queue may reuse it
    // with a different account/ref. Require the complete private identity too.
    return matchingQueueIndex(play, m_coordinator->exportQueue(), m_coordinator->queue());
}

QVariantList QueueHistoryStore::entries() const { return entriesForSource({}); }

QVariantList QueueHistoryStore::entriesForSource(const QString &sourceInstanceId) const
{
    QVariantList rows;
    const auto queue = m_coordinator ? m_coordinator->queue() : QVariantList{};
    const auto trusted = m_coordinator ? m_coordinator->exportQueue() : QList<QueueOccurrence>{};
    for (int i = 0; i < m_history.size() && i < m_historyKeys.size(); ++i) {
        const auto &play = m_history.at(i);
        if (!sourceInstanceId.isEmpty() && play.ref.sourceInstanceId != sourceInstanceId) continue;
        const int index = matchingQueueIndex(play, trusted, queue, false);
        rows.append(QVariantMap{
            {QStringLiteral("key"), m_historyKeys.at(i)},
            {QStringLiteral("title"), play.title},
            {QStringLiteral("artist"), play.artists.join(QStringLiteral(", "))},
            {QStringLiteral("album"), play.album},
            {QStringLiteral("duration"), qMax<qint64>(0, play.durationMs / 1000)},
            {QStringLiteral("playedAt"), play.playedAt},
            {QStringLiteral("sourceLabel"), index >= 0 && index < queue.size()
                ? queue.at(index).toMap().value(QStringLiteral("sourceLabel")) : QVariant{}},
            {QStringLiteral("replayable"), index >= 0
                && !queue.at(index).toMap().value(QStringLiteral("unavailable")).toBool()}});
    }
    return rows;
}

bool QueueHistoryStore::playEntry(const QString &key)
{
    const int record = m_historyKeys.indexOf(key);
    if (record < 0 || record >= m_history.size()) return false;
    const int index = queueIndex(m_history.at(record));
    return index >= 0 && m_coordinator && !m_coordinator->playQueueEntry(index).isNull();
}

QVariantMap QueueHistoryStore::latest() const
{
    if (m_history.isEmpty()) return {};
    const RecentPlay &item = m_history.first();
    return {{QStringLiteral("title"), item.title},
            {QStringLiteral("artist"), item.artists.join(QStringLiteral(", "))},
            {QStringLiteral("album"), item.album},
            {QStringLiteral("playedAt"), item.playedAt},
            {QStringLiteral("replayable"), latestQueueIndex() >= 0}};
}

bool QueueHistoryStore::playLatest()
{
    const int index = latestQueueIndex();
    return index >= 0 && m_coordinator && !m_coordinator->playQueueEntry(index).isNull();
}

void QueueHistoryStore::warn(const QString &key)
{
    if (m_warningKey == key) return;
    m_warningKey = key;
    emit warningChanged();
}

QString QueueHistoryStore::newBackupPath() const
{
    return m_filePath + QStringLiteral(".backup-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool QueueHistoryStore::loadAndAttach()
{
    if (m_attached || !m_coordinator || m_filePath.isEmpty()) return false;
    QFile file(m_filePath);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            m_readOnly = true;
            warn(QStringLiteral("music.queueHistoryReadFailed"));
            return false;
        }
        DecodeResult result;
        if (file.size() > maxBytes) result.status = QueueHistoryDecodeStatus::TooLarge;
        else result = QueueHistoryCodec::decode(file.read(maxBytes + 1));
        file.close();
        if (result.status == QueueHistoryDecodeStatus::UnsupportedVersion) {
            m_readOnly = true;
            warn(QStringLiteral("music.queueHistoryFutureVersion"));
        } else if (result.status != QueueHistoryDecodeStatus::Ok) {
            const QString path = newBackupPath();
            if (!m_hooks.backup(m_filePath, path)) {
                m_readOnly = true;
                warn(QStringLiteral("music.queueHistoryBackupFailed"));
                return false;
            }
            m_backupPath = path;
            warn(QStringLiteral("music.queueHistoryCorrupt"));
        } else {
            if (!m_coordinator->restoreQueue(result.snapshot.queue)) {
                m_readOnly = true;
                warn(QStringLiteral("music.queueHistoryRestoreFailed"));
                return false;
            }
            m_history = result.snapshot.history;
            m_historyKeys.clear();
            for (int i = 0; i < m_history.size(); ++i)
                m_historyKeys.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
            m_legacyImportVersion = result.snapshot.legacyImportVersion;
        }
    }
    connect(m_coordinator, &PlaybackCoordinator::queueChanged, this, [this] {
        persist();
        emit historyChanged();
    });
    connect(m_coordinator, &PlaybackCoordinator::playbackStarted, this, &QueueHistoryStore::recordStart);
    m_attached = true;
    emit historyChanged();
    return true;
}

void QueueHistoryStore::persist()
{
    if (!m_attached || m_readOnly || !m_coordinator) return;
    QueueHistorySnapshot snapshot;
    snapshot.queue = m_coordinator->exportQueue();
    snapshot.history = m_history;
    snapshot.legacyImportVersion = m_legacyImportVersion;
    const auto bytes = QueueHistoryCodec::encode(snapshot);
    if (!bytes || !m_hooks.write(m_filePath, *bytes)) {
        warn(QStringLiteral("music.queueHistoryWriteFailed"));
        return;
    }
    if (m_warningKey == QStringLiteral("music.queueHistoryWriteFailed")) warn({});
}

bool QueueHistoryStore::retrySave()
{
    if (!m_attached || m_readOnly || !m_coordinator) return false;
    persist();
    return m_warningKey != QStringLiteral("music.queueHistoryWriteFailed");
}

void QueueHistoryStore::recordStart(const QUuid &generation)
{
    if (!m_attached || !m_coordinator || generation.isNull()
        || generation != m_coordinator->currentGeneration()
        || generation == m_lastRecordedGeneration) return;
    const QUuid occurrence = m_coordinator->currentOccurrence();
    if (occurrence.isNull()) return;
    for (const auto &item : m_coordinator->exportQueue()) {
        if (item.occurrenceId != occurrence) continue;
        m_lastRecordedGeneration = generation;
        RecentPlay play;
        play.occurrenceId = occurrence;
        play.ref = item.ref;
        play.title = item.title;
        play.artists = item.artists;
        play.album = item.album;
        play.durationMs = item.durationMs;
        play.playedAt = QDateTime::currentDateTimeUtc();
        m_history.prepend(std::move(play));
        m_historyKeys.prepend(QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (m_history.size() > 200) m_history.resize(200);
        if (m_historyKeys.size() > 200) m_historyKeys.resize(200);
        persist();
        emit historyChanged();
        return;
    }
}

LegacyImportResult QueueHistoryStore::importLegacyFile(const QString &path)
{
    if (!m_attached || m_readOnly || !m_coordinator || m_legacyImportVersion != 0
        || path.isEmpty()) return {};
    const QFileInfo inputInfo(path);
    const QFileInfo activeInfo(m_filePath);
    if (inputInfo.absoluteFilePath() == activeInfo.absoluteFilePath()
        || (!inputInfo.canonicalFilePath().isEmpty()
            && inputInfo.canonicalFilePath() == activeInfo.canonicalFilePath())) return {};
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        warn(QStringLiteral("music.queueHistoryImportReadFailed"));
        return {};
    }
    const QByteArray bytes = input.read(maxBytes + 1);
    if (bytes.size() > maxBytes || !input.atEnd()) {
        warn(QStringLiteral("music.queueHistoryImportTooLarge"));
        return {};
    }
    input.close();
    const QString backup = newBackupPath();
    if (!m_hooks.backup(path, backup)) {
        warn(QStringLiteral("music.queueHistoryBackupFailed"));
        return {};
    }
    m_backupPath = backup;
    emit warningChanged();
    auto result = QueueHistoryCodec::importLegacy(bytes);
    if (!result.parsed) {
        warn(QStringLiteral("music.queueHistoryImportInvalid"));
        return result;
    }
    auto merged = m_coordinator->exportQueue();
    QSet<QUuid> ids;
    for (const auto &item : merged) ids.insert(item.occurrenceId);
    QList<QueueOccurrence> accepted;
    for (const auto &item : result.accepted) {
        if (merged.size() >= 1000 || ids.contains(item.occurrenceId)) {
            ++result.rejected;
            continue;
        }
        ids.insert(item.occurrenceId);
        merged.append(item);
        accepted.append(item);
    }
    result.accepted = accepted;
    if (!m_coordinator->restoreQueue(merged)) {
        warn(QStringLiteral("music.queueHistoryImportRestoreFailed"));
        result.parsed = false;
        return result;
    }
    m_legacyImportVersion = 1;
    persist();
    if (result.rejected > 0) warn(QStringLiteral("music.queueHistoryImportPartial"));
    return result;
}
