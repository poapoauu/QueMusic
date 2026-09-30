#include "LocalIdentityStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <utility>

namespace {
constexpr int kVersion = 2;
const QString kLegacy = QStringLiteral("__legacy__");

struct Membership {
    QSet<QString> tracks;
    QSet<QString> directories;
};

QSet<QString> keys(const QHash<QString, QString> &ids)
{
    QSet<QString> result;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) result.insert(it.key());
    return result;
}

bool readPaths(const QJsonValue &value, QSet<QString> *paths)
{
    if (!value.isArray()) return false;
    for (const auto &entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()) return false;
        paths->insert(entry.toString());
    }
    return true;
}

QJsonArray writePaths(const QSet<QString> &paths)
{
    QJsonArray result;
    QStringList sorted(paths.begin(), paths.end());
    sorted.sort();
    for (const QString &path : sorted) result.append(path);
    return result;
}

bool inRoot(const QString &path, const QString &root)
{
    if (root.isEmpty()) return true;
    const QString clean = QDir::cleanPath(root);
    return path == clean || path.startsWith(clean + QDir::separator());
}

bool readIds(const QJsonValue &value, QHash<QString, QString> *ids, QSet<QString> *seen)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key().isEmpty() || !it.value().isString()) return false;
        const QString id = it.value().toString();
        if (QUuid(id).isNull() || id != QUuid(id).toString(QUuid::WithoutBraces)
            || seen->contains(id)) return false;
        ids->insert(it.key(), id);
        seen->insert(id);
    }
    return true;
}

QJsonObject writeIds(const QHash<QString, QString> &ids)
{
    QJsonObject result;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

void fillIds(const QStringList &paths, const QHash<QString, QString> &oldIds,
             QHash<QString, QString> *nextIds, QSet<QString> *used)
{
    for (const QString &path : paths) {
        if (nextIds->contains(path)) continue;
        QString id = oldIds.value(path);
        if (id.isEmpty() || used->contains(id)) {
            do {
                id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            } while (used->contains(id));
        }
        nextIds->insert(path, id);
        used->insert(id);
    }
}
}

LocalIdentityStore::LocalIdentityStore(QString filePath, QString sourceInstanceId,
                                       QString configurationKey, QString scanRoot)
    : m_filePath(std::move(filePath)), m_sourceInstanceId(std::move(sourceInstanceId)),
      m_configurationKey(configurationKey.isEmpty() ? QStringLiteral("__default__")
                                                 : std::move(configurationKey)),
      m_scanRoot(std::move(scanRoot))
{
}

bool LocalIdentityStore::readCurrent(LocalIdentitySnapshot *out) const
{
    if (!out) return false;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != kVersion)
        || root.value(QStringLiteral("sourceInstanceId")).toString() != m_sourceInstanceId)
        return false;
    LocalIdentitySnapshot parsed;
    QSet<QString> seen;
    if (!readIds(root.value(QStringLiteral("tracks")), &parsed.trackIdsByPath, &seen)
        || !readIds(root.value(QStringLiteral("directories")), &parsed.directoryIdsByPath, &seen))
        return false;
    if (version == kVersion) {
        const QJsonValue memberships = root.value(QStringLiteral("memberships"));
        if (!memberships.isObject()) return false;
        const QJsonObject members = memberships.toObject();
        QSet<QString> tracks, directories;
        for (auto it = members.begin(); it != members.end(); ++it) {
            if (it.key().isEmpty() || !it.value().isObject()) return false;
            const QJsonObject member = it.value().toObject();
            QSet<QString> memberTracks, memberDirectories;
            if (!readPaths(member.value(QStringLiteral("tracks")), &memberTracks)
                || !readPaths(member.value(QStringLiteral("directories")), &memberDirectories))
                return false;
            tracks.unite(memberTracks);
            directories.unite(memberDirectories);
        }
        if (tracks != keys(parsed.trackIdsByPath)
            || directories != keys(parsed.directoryIdsByPath)) return false;
    }
    *out = std::move(parsed);
    return true;
}

bool LocalIdentityStore::reconcile(const QStringList &trackPaths, const QStringList &directoryPaths,
                                   LocalIdentitySnapshot *out, QString *errorKey)
{
    if (errorKey) errorKey->clear();
    if (!out || m_filePath.isEmpty() || m_sourceInstanceId.isEmpty()) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.invalidInput");
        return false;
    }

    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.writeFailed");
        return false;
    }
    QLockFile lock(m_filePath + QStringLiteral(".lock"));
    if (!lock.tryLock(5000)) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.writeFailed");
        return false;
    }

    LocalIdentitySnapshot previous;
    QHash<QString, Membership> memberships;
    if (QFile::exists(m_filePath)) {
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (errorKey) *errorKey = QStringLiteral("local.identity.readFailed");
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject root = document.object();
            if (root.value(QStringLiteral("sourceInstanceId")).isString()
                && root.value(QStringLiteral("sourceInstanceId")).toString() != m_sourceInstanceId) {
                if (errorKey) *errorKey = QStringLiteral("local.identity.instanceMismatch");
                return false;
            }
            const QJsonValue version = root.value(QStringLiteral("version"));
            if (version.isDouble() && version.toDouble() > kVersion) {
                if (errorKey) *errorKey = QStringLiteral("local.identity.futureVersion");
                return false;
            }
            QSet<QString> seen;
            const int fileVersion = version.toInt();
            if ((fileVersion != 1 && fileVersion != kVersion)
                || root.value(QStringLiteral("sourceInstanceId")).toString() != m_sourceInstanceId
                || !readIds(root.value(QStringLiteral("tracks")), &previous.trackIdsByPath, &seen)
                || !readIds(root.value(QStringLiteral("directories")), &previous.directoryIdsByPath, &seen)) {
                previous = {};
            } else if (fileVersion == 1) {
                memberships[kLegacy] = {keys(previous.trackIdsByPath),
                                        keys(previous.directoryIdsByPath)};
            } else {
                const QJsonValue membersValue = root.value(QStringLiteral("memberships"));
                bool valid = membersValue.isObject();
                if (valid) {
                    const QJsonObject members = membersValue.toObject();
                    for (auto it = members.begin(); it != members.end(); ++it) {
                        if (it.key().isEmpty() || !it.value().isObject()) { valid = false; break; }
                        const QJsonObject item = it.value().toObject();
                        Membership member;
                        if (!readPaths(item.value(QStringLiteral("tracks")), &member.tracks)
                            || !readPaths(item.value(QStringLiteral("directories")), &member.directories)) {
                            valid = false;
                            break;
                        }
                        memberships.insert(it.key(), std::move(member));
                    }
                }
                QSet<QString> allTracks, allDirectories;
                for (const auto &member : std::as_const(memberships)) {
                    allTracks.unite(member.tracks);
                    allDirectories.unite(member.directories);
                }
                if (!valid || allTracks != keys(previous.trackIdsByPath)
                    || allDirectories != keys(previous.directoryIdsByPath)) {
                    previous = {};
                    memberships.clear();
                }
            }
        }
        // A malformed private index has no trustworthy identity bindings.
    }

    Membership current = memberships.take(m_configurationKey);
    if (m_configurationKey != kLegacy && memberships.contains(kLegacy)) {
        Membership legacy = memberships.take(kLegacy);
        for (const QString &path : std::as_const(legacy.tracks))
            if (inRoot(path, m_scanRoot)) current.tracks.insert(path);
        for (const QString &path : std::as_const(legacy.directories))
            if (inRoot(path, m_scanRoot)) current.directories.insert(path);
        legacy.tracks.subtract(current.tracks);
        legacy.directories.subtract(current.directories);
        if (!legacy.tracks.isEmpty() || !legacy.directories.isEmpty())
            memberships.insert(kLegacy, std::move(legacy));
    }
    const QSet<QString> observedTracks(trackPaths.begin(), trackPaths.end());
    const QSet<QString> observedDirectories(directoryPaths.begin(), directoryPaths.end());
    const QSet<QString> removedTracks = current.tracks - observedTracks;
    const QSet<QString> removedDirectories = current.directories - observedDirectories;
    for (auto it = memberships.begin(); it != memberships.end(); ++it) {
        it->tracks.subtract(removedTracks);
        it->directories.subtract(removedDirectories);
    }
    for (const QString &path : removedTracks) previous.trackIdsByPath.remove(path);
    for (const QString &path : removedDirectories) previous.directoryIdsByPath.remove(path);
    current.tracks = observedTracks;
    current.directories = observedDirectories;
    memberships.insert(m_configurationKey, std::move(current));
    LocalIdentitySnapshot next = previous;
    QSet<QString> used;
    for (const QString &id : std::as_const(next.trackIdsByPath)) used.insert(id);
    for (const QString &id : std::as_const(next.directoryIdsByPath)) used.insert(id);
    fillIds(trackPaths, previous.trackIdsByPath, &next.trackIdsByPath, &used);
    fillIds(directoryPaths, previous.directoryIdsByPath, &next.directoryIdsByPath, &used);
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("sourceInstanceId"), m_sourceInstanceId);
    root.insert(QStringLiteral("tracks"), writeIds(next.trackIdsByPath));
    root.insert(QStringLiteral("directories"), writeIds(next.directoryIdsByPath));
    QJsonObject members;
    for (auto it = memberships.cbegin(); it != memberships.cend(); ++it) {
        QJsonObject item;
        item.insert(QStringLiteral("tracks"), writePaths(it->tracks));
        item.insert(QStringLiteral("directories"), writePaths(it->directories));
        members.insert(it.key(), item);
    }
    root.insert(QStringLiteral("memberships"), members);
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.writeFailed");
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.writeFailed");
        return false;
    }
    *out = std::move(next);
    return true;
}
