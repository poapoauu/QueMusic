#include "LocalIdentityStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <utility>

namespace {
constexpr int kVersion = 1;

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

LocalIdentityStore::LocalIdentityStore(QString filePath, QString sourceInstanceId)
    : m_filePath(std::move(filePath)), m_sourceInstanceId(std::move(sourceInstanceId))
{
}

bool LocalIdentityStore::reconcile(const QStringList &trackPaths, const QStringList &directoryPaths,
                                   LocalIdentitySnapshot *out, QString *errorKey)
{
    if (errorKey) errorKey->clear();
    if (!out || m_filePath.isEmpty() || m_sourceInstanceId.isEmpty()) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.invalidInput");
        return false;
    }

    LocalIdentitySnapshot previous;
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
            if (version.toInt() != kVersion
                || root.value(QStringLiteral("sourceInstanceId")).toString() != m_sourceInstanceId
                || !readIds(root.value(QStringLiteral("tracks")), &previous.trackIdsByPath, &seen)
                || !readIds(root.value(QStringLiteral("directories")), &previous.directoryIdsByPath, &seen)) {
                previous = {};
            }
        }
        // A malformed private index has no trustworthy identity bindings.
    }

    LocalIdentitySnapshot next;
    QSet<QString> used;
    fillIds(trackPaths, previous.trackIdsByPath, &next.trackIdsByPath, &used);
    fillIds(directoryPaths, previous.directoryIdsByPath, &next.directoryIdsByPath, &used);
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("sourceInstanceId"), m_sourceInstanceId);
    root.insert(QStringLiteral("tracks"), writeIds(next.trackIdsByPath));
    root.insert(QStringLiteral("directories"), writeIds(next.directoryIdsByPath));
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) {
        if (errorKey) *errorKey = QStringLiteral("local.identity.writeFailed");
        return false;
    }
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
