#include "QueueHistoryCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace {
constexpr int maxQueue = 1000;
constexpr int maxHistory = 200;
constexpr int maxDisplay = 512;
constexpr int maxArtists = 16;
constexpr int maxIdentity = 1024;
constexpr qsizetype maxBytes = 16 * 1024 * 1024;

using Status = QueueHistoryDecodeStatus;

bool onlyKeys(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    if (object.size() != static_cast<int>(keys.size())) return false;
    for (const char *key : keys) {
        if (!object.contains(QLatin1String(key))) return false;
    }
    return true;
}

bool validUnicode(const QString &value, int maxCharacters)
{
    int characters = 0;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch.isHighSurrogate()) {
            if (++i >= value.size() || !value.at(i).isLowSurrogate()) return false;
        } else if (ch.isLowSurrogate()) {
            return false;
        }
        if (++characters > maxCharacters) return false;
    }
    return true;
}

bool directLocator(const QString &id)
{
    if (id.startsWith('/') || id.startsWith('\\') || id.startsWith("./")
        || id.startsWith("../") || id.startsWith(".\\")
        || id.startsWith("..\\") || id.startsWith("~/")
        || id.startsWith("~\\")) return true;
    // Any URI scheme or Windows drive prefix is a locator, including schemes
    // the Host does not currently know how to play.
    const int colon = id.indexOf(':');
    if (colon <= 0) return false;
    if (!id.at(0).isLetter()) return false;
    for (int i = 1; i < colon; ++i) {
        const QChar ch = id.at(i);
        if (!ch.isLetterOrNumber() && ch != '+' && ch != '-' && ch != '.') return false;
    }
    return true;
}

Status textField(const QJsonValue &value, QString *out, int limit, bool required = false)
{
    if (!value.isString()) return Status::Corrupt;
    const QString text = value.toString();
    if (!validUnicode(text, limit)) return Status::TooLarge;
    if (required && text.trimmed().isEmpty()) return Status::Corrupt;
    *out = text;
    return Status::Ok;
}

bool validRef(const MediaRefV2 &ref)
{
    return !ref.sourcePluginId.trimmed().isEmpty()
        && !ref.sourceInstanceId.trimmed().isEmpty()
        && !ref.entityId.trimmed().isEmpty()
        && validUnicode(ref.sourcePluginId, maxIdentity)
        && validUnicode(ref.sourceInstanceId, maxIdentity)
        && validUnicode(ref.accountId, maxIdentity)
        && validUnicode(ref.entityId, maxIdentity)
        && !directLocator(ref.entityId)
        && static_cast<int>(ref.entityType) >= static_cast<int>(MediaEntityTypeV2::Track)
        && static_cast<int>(ref.entityType) <= static_cast<int>(MediaEntityTypeV2::Directory);
}

QJsonObject writeRef(const MediaRefV2 &ref)
{
    return {{"sourcePluginId", ref.sourcePluginId},
            {"sourceInstanceId", ref.sourceInstanceId},
            {"accountId", ref.accountId},
            {"entityType", static_cast<int>(ref.entityType)},
            {"entityId", ref.entityId}};
}

Status readRef(const QJsonValue &value, MediaRefV2 *out)
{
    if (!value.isObject()) return Status::Corrupt;
    const auto object = value.toObject();
    if (!onlyKeys(object, {"sourcePluginId", "sourceInstanceId", "accountId", "entityType", "entityId"}))
        return Status::Corrupt;
    Status status = textField(object.value("sourcePluginId"), &out->sourcePluginId, maxIdentity, true);
    if (status != Status::Ok) return status;
    status = textField(object.value("sourceInstanceId"), &out->sourceInstanceId, maxIdentity, true);
    if (status != Status::Ok) return status;
    status = textField(object.value("accountId"), &out->accountId, maxIdentity);
    if (status != Status::Ok) return status;
    status = textField(object.value("entityId"), &out->entityId, maxIdentity, true);
    if (status != Status::Ok) return status;
    const auto type = object.value("entityType");
    if (!type.isDouble() || type.toInteger(-1) < 0
        || type.toInteger(-1) > static_cast<int>(MediaEntityTypeV2::Directory)) return Status::Corrupt;
    out->entityType = static_cast<MediaEntityTypeV2>(type.toInteger());
    return directLocator(out->entityId) ? Status::Corrupt : Status::Ok;
}

bool validPresentation(const QString &title, const QStringList &artists,
                       const QString &album, qint64 durationMs)
{
    if (artists.size() > maxArtists || !validUnicode(title, maxDisplay)
        || !validUnicode(album, maxDisplay) || durationMs < 0
        || durationMs > 9007199254740991LL) return false;
    for (const auto &artist : artists) {
        if (!validUnicode(artist, maxDisplay)) return false;
    }
    return true;
}

QJsonObject writePresentation(const QUuid &id, const MediaRefV2 &ref,
                              const QString &title, const QStringList &artists,
                              const QString &album, qint64 durationMs)
{
    QJsonArray artistArray;
    for (const auto &artist : artists) artistArray.append(artist);
    return {{"occurrenceId", id.toString(QUuid::WithoutBraces)},
            {"ref", writeRef(ref)}, {"title", title}, {"artists", artistArray},
            {"album", album}, {"durationMs", static_cast<double>(durationMs)}};
}

Status readPresentation(const QJsonObject &object, QUuid *id, MediaRefV2 *ref,
                        QString *title, QStringList *artists, QString *album,
                        qint64 *durationMs)
{
    QString idText;
    Status status = textField(object.value("occurrenceId"), &idText, 36, true);
    if (status != Status::Ok) return status;
    *id = QUuid(idText);
    if (id->isNull() || idText != id->toString(QUuid::WithoutBraces)) return Status::Corrupt;
    status = readRef(object.value("ref"), ref);
    if (status != Status::Ok) return status;
    status = textField(object.value("title"), title, maxDisplay);
    if (status != Status::Ok) return status;
    status = textField(object.value("album"), album, maxDisplay);
    if (status != Status::Ok) return status;
    if (!object.value("artists").isArray()) return Status::Corrupt;
    const auto array = object.value("artists").toArray();
    if (array.size() > maxArtists) return Status::TooLarge;
    for (const auto &artist : array) {
        QString name;
        status = textField(artist, &name, maxDisplay);
        if (status != Status::Ok) return status;
        artists->append(name);
    }
    const auto duration = object.value("durationMs");
    if (!duration.isDouble()) return Status::Corrupt;
    *durationMs = duration.toInteger(-1);
    if (*durationMs < 0 || *durationMs > 9007199254740991LL) return Status::Corrupt;
    return Status::Ok;
}

Status readQueueItem(const QJsonValue &value, QueueOccurrence *out)
{
    if (!value.isObject()) return Status::Corrupt;
    const auto object = value.toObject();
    if (!onlyKeys(object, {"occurrenceId", "ref", "title", "artists", "album", "durationMs", "playableAtEnqueue"})
        || !object.value("playableAtEnqueue").isBool()) return Status::Corrupt;
    const Status status = readPresentation(object, &out->occurrenceId, &out->ref, &out->title,
                                           &out->artists, &out->album, &out->durationMs);
    if (status == Status::Ok) out->playableAtEnqueue = object.value("playableAtEnqueue").toBool();
    return status;
}

Status readHistoryItem(const QJsonValue &value, RecentPlay *out)
{
    if (!value.isObject()) return Status::Corrupt;
    const auto object = value.toObject();
    if (!onlyKeys(object, {"occurrenceId", "ref", "title", "artists", "album", "durationMs", "playedAt"}))
        return Status::Corrupt;
    Status status = readPresentation(object, &out->occurrenceId, &out->ref, &out->title,
                                     &out->artists, &out->album, &out->durationMs);
    if (status != Status::Ok) return status;
    QString timestamp;
    status = textField(object.value("playedAt"), &timestamp, 24, true);
    if (status != Status::Ok) return status;
    out->playedAt = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    if (!out->playedAt.isValid() || out->playedAt.timeSpec() != Qt::UTC
        || out->playedAt.toString(Qt::ISODateWithMs) != timestamp) return Status::Corrupt;
    return Status::Ok;
}

QJsonObject writeQueueItem(const QueueOccurrence &item)
{
    auto object = writePresentation(item.occurrenceId, item.ref, item.title,
                                    item.artists, item.album, item.durationMs);
    object.insert("playableAtEnqueue", item.playableAtEnqueue);
    return object;
}

QJsonObject writeHistoryItem(const RecentPlay &item)
{
    auto object = writePresentation(item.occurrenceId, item.ref, item.title,
                                    item.artists, item.album, item.durationMs);
    object.insert("playedAt", item.playedAt.toUTC().toString(Qt::ISODateWithMs));
    return object;
}
}

std::optional<QByteArray> QueueHistoryCodec::encode(const QueueHistorySnapshot &snapshot)
{
    if (snapshot.queue.size() > maxQueue || snapshot.history.size() > maxHistory
        || snapshot.legacyImportVersion < 0 || snapshot.legacyImportVersion > 1) return std::nullopt;
    QJsonArray queue;
    QSet<QUuid> occurrenceIds;
    for (const auto &item : snapshot.queue) {
        if (item.occurrenceId.isNull() || occurrenceIds.contains(item.occurrenceId)
            || !validRef(item.ref)
            || !validPresentation(item.title, item.artists, item.album, item.durationMs)) return std::nullopt;
        occurrenceIds.insert(item.occurrenceId);
        queue.append(writeQueueItem(item));
    }
    QJsonArray history;
    for (const auto &item : snapshot.history) {
        if (item.occurrenceId.isNull() || !validRef(item.ref) || !item.playedAt.isValid()
            || !validPresentation(item.title, item.artists, item.album, item.durationMs)) return std::nullopt;
        history.append(writeHistoryItem(item));
    }
    const QByteArray result = QJsonDocument(QJsonObject{{"schemaVersion", 1},
                                                   {"legacyImportVersion", snapshot.legacyImportVersion},
                                                   {"queue", queue}, {"history", history}})
                                  .toJson(QJsonDocument::Compact);
    return result.size() <= maxBytes ? std::optional<QByteArray>(result) : std::nullopt;
}

DecodeResult QueueHistoryCodec::decode(const QByteArray &bytes)
{
    if (bytes.size() > maxBytes) return {Status::TooLarge, {}};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {Status::Corrupt, {}};
    const auto root = document.object();
    const auto version = root.value("schemaVersion");
    if (!version.isDouble() || version.toInteger(-1) < 0) return {Status::Corrupt, {}};
    if (version.toInteger() > 1) return {Status::UnsupportedVersion, {}};
    if (version.toInteger() != 1
        || !onlyKeys(root, {"schemaVersion", "legacyImportVersion", "queue", "history"})
        || !root.value("queue").isArray() || !root.value("history").isArray()) return {Status::Corrupt, {}};
    const auto importVersion = root.value("legacyImportVersion");
    if (!importVersion.isDouble() || importVersion.toInteger(-1) < 0
        || importVersion.toInteger(-1) > 1) return {Status::Corrupt, {}};
    const auto queue = root.value("queue").toArray();
    const auto history = root.value("history").toArray();
    if (queue.size() > maxQueue || history.size() > maxHistory) return {Status::TooLarge, {}};
    QueueHistorySnapshot snapshot;
    snapshot.legacyImportVersion = static_cast<int>(importVersion.toInteger());
    QSet<QUuid> occurrenceIds;
    for (const auto &value : queue) {
        QueueOccurrence item;
        const Status status = readQueueItem(value, &item);
        if (status != Status::Ok) return {status, {}};
        if (occurrenceIds.contains(item.occurrenceId)) return {Status::Corrupt, {}};
        occurrenceIds.insert(item.occurrenceId);
        snapshot.queue.append(item);
    }
    for (const auto &value : history) {
        RecentPlay item;
        const Status status = readHistoryItem(value, &item);
        if (status != Status::Ok) return {status, {}};
        snapshot.history.append(item);
    }
    return {Status::Ok, snapshot};
}

LegacyImportResult QueueHistoryCodec::importLegacy(const QByteArray &bytes)
{
    LegacyImportResult result;
    if (bytes.size() > maxBytes) return result;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return result;
    const auto root = document.object();
    if (!root.value("queue").isArray()) return result;
    const auto queue = root.value("queue").toArray();
    result.parsed = true;
    QSet<QUuid> ids;
    for (const auto &value : queue) {
        if (!value.isObject()) {
            ++result.rejected;
            continue;
        }
        const auto legacy = value.toObject();
        // Legacy presentation may have extra fields. Project only the fields
        // understood by this codec; the five-part ref must already be present.
        const auto optional = [&legacy](const char *key, const QJsonValue &fallback) {
            return legacy.contains(QLatin1String(key)) ? legacy.value(QLatin1String(key)) : fallback;
        };
        QJsonObject projected{{"occurrenceId", legacy.contains("occurrenceId")
                                                   ? legacy.value("occurrenceId")
                                                   : QJsonValue(QUuid::createUuid().toString(QUuid::WithoutBraces))},
                              {"ref", legacy.value("ref")},
                              {"title", optional("title", QString())},
                              {"artists", optional("artists", QJsonArray{})},
                              {"album", optional("album", QString())},
                              {"durationMs", optional("durationMs", 0)},
                              {"playableAtEnqueue", optional("playableAtEnqueue", false)}};
        QueueOccurrence item;
        if (readQueueItem(projected, &item) != Status::Ok || ids.contains(item.occurrenceId)
            || result.accepted.size() >= maxQueue) {
            ++result.rejected;
            continue;
        }
        ids.insert(item.occurrenceId);
        result.accepted.append(item);
    }
    return result;
}
