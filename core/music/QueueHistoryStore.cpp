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
}

QString QueueHistoryStore::warningKey() const { return m_warningKey; }
QString QueueHistoryStore::backupPath() const { return m_backupPath; }
QList<RecentPlay> QueueHistoryStore::history() const { return m_history; }

int QueueHistoryStore::latestQueueIndex() const
{
    if (!m_coordinator || m_history.isEmpty()) return -1;
    const auto queue = m_coordinator->queue();
    const QUuid occurrence = m_history.first().occurrenceId;
    for (int index = 0; index < queue.size(); ++index) {
        const QVariantMap row = queue.at(index).toMap();
        if (row.value(QStringLiteral("occurrenceId")).toUuid() == occurrence
            && !row.value(QStringLiteral("unavailable")).toBool()) return index;
    }
    return -1;
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
        if (m_history.size() > 200) m_history.resize(200);
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
