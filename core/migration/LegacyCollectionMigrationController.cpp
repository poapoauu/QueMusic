#include "LegacyCollectionMigrationController.h"

#include "SourceRegistry.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
QString pathDigest(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),
        QCryptographicHash::Sha256).toHex());
}
QString connectionName()
{
    return QStringLiteral("collection_status_")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

LegacyCollectionMigrationController::LegacyCollectionMigrationController(
    SourceRegistry *registry, QString sourceDatabase, QString destinationDatabase,
    QString backupDatabase, QObject *parent)
    : QObject(parent), m_registry(registry), m_sourceDatabase(std::move(sourceDatabase)),
      m_destinationDatabase(std::move(destinationDatabase)),
      m_backupDatabase(std::move(backupDatabase)), m_resolver(registry),
      m_migration(&m_resolver) {}

QVariantList LegacyCollectionMigrationController::candidates() const
{
    QVariantList result;
    if (!m_registry) return result;
    for (const auto &instance : m_registry->enabledInstances()) {
        if (!instance.enabled || instance.sourceId != QLatin1String("local")
            || instance.pluginPackageId != QLatin1String("org.quemusic.source.local"))
            continue;
        result.append(QVariantMap{{QStringLiteral("instanceId"), instance.sourceInstanceId},
                                  {QStringLiteral("displayName"), instance.displayName
                                      + QStringLiteral(" (") + instance.sourceInstanceId
                                      + QLatin1Char(')')}});
    }
    return result;
}

QVariantMap LegacyCollectionMigrationController::preview() const
{
    QVariantMap result{{QStringLiteral("folderCount"), 0},
                       {QStringLiteral("songCount"), 0}};
    if (!QFileInfo(m_sourceDatabase).isFile()) {
        result.insert(QStringLiteral("errorKey"), QStringLiteral("local.collectionMigration.sourceUnavailable"));
        return result;
    }
    const QString connection = QStringLiteral("collection_preview_")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool okay = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(m_sourceDatabase);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            okay = query.exec(QStringLiteral("SELECT COUNT(*) FROM folders WHERE type='my'"))
                && query.next();
            if (okay) result.insert(QStringLiteral("folderCount"), query.value(0).toInt());
            if (okay) {
                okay = query.exec(QStringLiteral(
                    "SELECT COUNT(*) FROM songs s JOIN folders f ON f.id=s.folder_id "
                    "WHERE f.type='my'")) && query.next();
                if (okay) result.insert(QStringLiteral("songCount"), query.value(0).toInt());
            }
            query.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (!okay) result.insert(QStringLiteral("errorKey"),
                             QStringLiteral("local.collectionMigration.sourceUnavailable"));
    return result;
}

QVariantMap LegacyCollectionMigrationController::run(const QString &candidateInstanceId)
{
    bool allowed = false;
    for (const auto &candidate : candidates()) {
        if (candidate.toMap().value(QStringLiteral("instanceId")).toString()
            == candidateInstanceId) {
            allowed = true;
            break;
        }
    }
    if (!allowed) return {{QStringLiteral("committed"), false},
                          {QStringLiteral("errorKey"),
                           QStringLiteral("local.collectionMigration.invalidCandidate")}};
    const auto result = m_migration.run(m_sourceDatabase, m_destinationDatabase,
                                         m_backupDatabase, {candidateInstanceId});
    if (result.committed) {
        ++m_revision;
        emit revisionChanged();
    }
    return {{QStringLiteral("committed"), result.committed},
            {QStringLiteral("errorKey"), result.errorKey},
            {QStringLiteral("matched"), result.matched},
            {QStringLiteral("noMatch"), result.noMatch},
            {QStringLiteral("ambiguous"), result.ambiguous},
            {QStringLiteral("unavailable"), result.unavailable},
            {QStringLiteral("invalidPath"), result.invalidPath}};
}

QString LegacyCollectionMigrationController::songStatus(int folderId, int songId) const
{
    if (folderId < 0 || songId < 0) return QStringLiteral("unavailable");
    if (!QFileInfo::exists(m_destinationDatabase)) return QStringLiteral("notMigrated");
    const QString sourcePath = QFileInfo(m_sourceDatabase).canonicalFilePath();
    if (sourcePath.isEmpty()) return QStringLiteral("unavailable");

    QString status = QStringLiteral("unavailable");
    QString storedDigest;
    QVariantMap storedRef;
    const QString destinationConnection = connectionName();
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), destinationConnection);
        db.setDatabaseName(m_destinationDatabase);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT key,value FROM meta WHERE key IN "
                                         "('schemaVersion','sourceDatabase')"));
            QString version;
            QString sourceKey;
            if (query.exec()) {
                while (query.next()) {
                    if (query.value(0).toString() == QLatin1String("schemaVersion"))
                        version = query.value(1).toString();
                    else sourceKey = query.value(1).toString();
                }
            }
            if (version == QLatin1String("1") && sourceKey == pathDigest(sourcePath)) {
                query.prepare(QStringLiteral("SELECT status,path_digest,ref_json FROM members "
                                             "WHERE folder_id=? AND legacy_song_id=?"));
                query.addBindValue(folderId);
                query.addBindValue(songId);
                if (query.exec()) {
                    status = QStringLiteral("pending");
                    if (query.next()) {
                        const QString stored = query.value(0).toString();
                        const QStringList recognized{QStringLiteral("matched"),
                            QStringLiteral("noMatch"), QStringLiteral("ambiguous"),
                            QStringLiteral("unavailable"), QStringLiteral("invalidPath")};
                        status = recognized.contains(stored) ? stored : QStringLiteral("unavailable");
                        storedDigest = query.value(1).toString();
                        storedRef = QJsonDocument::fromJson(query.value(2).toByteArray())
                            .object().toVariantMap();
                    }
                    if (query.lastError().isValid()) status = QStringLiteral("unavailable");
                }
            }
            query.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(destinationConnection);
    if (status != QLatin1String("matched")) return status;

    const QString sourceConnection = connectionName();
    bool unchanged = false;
    QUrl oldUrl;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), sourceConnection);
        db.setDatabaseName(sourcePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT path FROM songs WHERE folder_id=? AND id=?"));
            query.addBindValue(folderId);
            query.addBindValue(songId);
            if (query.exec() && query.next()) {
                const QString oldPath = query.value(0).toString();
                oldUrl = oldPath.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)
                    ? QUrl(oldPath, QUrl::StrictMode) : QUrl::fromLocalFile(oldPath);
                unchanged = pathDigest(oldPath) == storedDigest && oldUrl.isLocalFile()
                    && QFileInfo(oldUrl.toLocalFile()).isFile();
            }
            query.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(sourceConnection);
    if (!unchanged) return QStringLiteral("stale");
    if (!m_registry || storedRef.value(QStringLiteral("sourcePluginId")).toString() != QLatin1String("local")
        || storedRef.value(QStringLiteral("entityId")).toString().isEmpty()
        || storedRef.value(QStringLiteral("entityType")).toInt() != int(MediaEntityTypeV2::Track))
        return QStringLiteral("unavailable");
    int matches = 0;
    for (const auto &instance : m_registry->enabledInstances()) {
        if (instance.sourceInstanceId != storedRef.value(QStringLiteral("sourceInstanceId")).toString())
            continue;
        if (!instance.enabled || instance.state != SourceSessionStateV2::Ready
            || instance.sourceId != QLatin1String("local")
            || instance.pluginPackageId != QLatin1String("org.quemusic.source.local")
            || instance.accountId != storedRef.value(QStringLiteral("accountId")).toString())
            return QStringLiteral("unavailable");
        ++matches;
    }
    if (matches != 1) return QStringLiteral("unavailable");
    const auto claimed = m_resolver.resolve(oldUrl,
        {storedRef.value(QStringLiteral("sourceInstanceId")).toString()});
    return claimed.status == LegacyMediaIdentityResolver::Status::Matched
        && mediaRefV2ToVariantMap(claimed.ref) == storedRef
        ? status : QStringLiteral("unavailable");
}
