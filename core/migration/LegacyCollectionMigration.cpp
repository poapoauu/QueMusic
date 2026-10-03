#include "LegacyCollectionMigration.h"

#include "v2/SourceV2Types.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
struct Song {
    qlonglong folderId = 0;
    qlonglong id = 0;
    QString title;
    QString path;
    QString artist;
    int duration = 0;
};
struct Folder { qlonglong id = 0; QString title; };

QString connectionName()
{
    return QStringLiteral("collection_migration_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString digest(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QUrl legacyUrl(const QString &value)
{
    if (value.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return QUrl(value, QUrl::StrictMode);
    if (!QDir::isAbsolutePath(value)) return {};
    return QUrl::fromLocalFile(value);
}
QString statusName(LegacyMediaIdentityResolver::Status status)
{
    using Status = LegacyMediaIdentityResolver::Status;
    switch (status) {
    case Status::Matched: return QStringLiteral("matched");
    case Status::NoMatch: return QStringLiteral("noMatch");
    case Status::Ambiguous: return QStringLiteral("ambiguous");
    case Status::Unavailable: return QStringLiteral("unavailable");
    case Status::InvalidRequest: return QStringLiteral("invalidPath");
    }
    return QStringLiteral("invalidPath");
}
bool validRef(const QVariantMap &map)
{
    for (const auto key : {"sourcePluginId", "sourceInstanceId", "accountId", "entityId"})
        if (map.value(QLatin1String(key)).toString().trimmed().isEmpty()) return false;
    bool validType = false;
    const int type = map.value(QStringLiteral("entityType")).toInt(&validType);
    return validType && type == int(MediaEntityTypeV2::Track);
}
}

LegacyCollectionMigration::LegacyCollectionMigration(const LegacyMediaIdentityResolver *resolver)
    : m_resolver(resolver) {}

LegacyCollectionMigration::Result LegacyCollectionMigration::run(
    const QString &sourceDatabase, const QString &destinationDatabase,
    const QString &backupDatabase, const QStringList &candidateInstanceIds) const
{
    Result result;
    const QString sourcePath = QFileInfo(sourceDatabase).canonicalFilePath();
    const QString destinationPath = QFileInfo(destinationDatabase).absoluteFilePath();
    const QString backupPath = QFileInfo(backupDatabase).absoluteFilePath();
    const QString existingDestination = QFileInfo(destinationDatabase).canonicalFilePath();
    const QString existingBackup = QFileInfo(backupDatabase).canonicalFilePath();
    if (!m_resolver || candidateInstanceIds.isEmpty() || sourcePath.isEmpty()
        || destinationDatabase.isEmpty() || backupDatabase.isEmpty()
        || sourcePath == destinationPath || sourcePath == backupPath
        || destinationPath == backupPath || existingDestination == sourcePath
        || existingBackup == sourcePath
        || (!existingDestination.isEmpty() && existingDestination == existingBackup)
        || !QFileInfo(sourcePath).isFile()) {
        result.errorKey = QStringLiteral("local.collectionMigration.invalidRequest");
        return result;
    }
    const QString sourceKey = digest(sourcePath);
    const bool destinationExisted = QFileInfo::exists(destinationPath);
    const QString stagedDestination = destinationExisted ? QString() : destinationPath
        + QStringLiteral(".tmp-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QList<Folder> folders;
    QList<Song> songs;
    const QString sourceConnection = connectionName();
    bool sourceOkay = false;
    bool backupOkay = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), sourceConnection);
        db.setDatabaseName(sourcePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            sourceOkay = query.exec(QStringLiteral(
                "SELECT id,name FROM folders WHERE type='my' ORDER BY id"));
            if (sourceOkay) {
                while (query.next()) folders.append({query.value(0).toLongLong(), query.value(1).toString()});
                sourceOkay = !query.lastError().isValid();
            }
            if (sourceOkay) {
                sourceOkay = query.exec(QStringLiteral(
                    "SELECT s.folder_id,s.id,s.name,s.path,s.singer,s.duration "
                    "FROM songs s JOIN folders f ON f.id=s.folder_id "
                    "WHERE f.type='my' ORDER BY s.folder_id,s.id"));
                if (sourceOkay) {
                    while (query.next()) songs.append({query.value(0).toLongLong(),
                        query.value(1).toLongLong(), query.value(2).toString(),
                        query.value(3).toString(), query.value(4).toString(),
                        query.value(5).toInt()});
                    sourceOkay = !query.lastError().isValid();
                }
            }
            query.finish();
            if (sourceOkay && !QFileInfo::exists(backupPath)) {
                const QString staged = backupPath + QStringLiteral(".tmp-")
                    + QUuid::createUuid().toString(QUuid::WithoutBraces);
                QSqlQuery backup(db);
                backupOkay = backup.prepare(QStringLiteral("VACUUM INTO ?"));
                if (backupOkay) {
                    backup.addBindValue(staged);
                    backupOkay = backup.exec();
                }
                if (backupOkay) backupOkay = QFile::rename(staged, backupPath);
                if (!backupOkay) QFile::remove(staged);
            } else if (sourceOkay) {
                const QString backupConnection = connectionName();
                {
                    auto existing = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), backupConnection);
                    existing.setDatabaseName(backupPath);
                    existing.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
                    backupOkay = existing.open() && existing.tables().contains(QStringLiteral("songs"));
                    existing.close();
                }
                QSqlDatabase::removeDatabase(backupConnection);
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(sourceConnection);
    if (!sourceOkay || !backupOkay) {
        result.errorKey = sourceOkay ? QStringLiteral("local.collectionMigration.backupFailed")
                                     : QStringLiteral("local.collectionMigration.sourceUnavailable");
        return result;
    }

    const QString destinationConnection = connectionName();
    bool committed = false;
    QString errorKey;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), destinationConnection);
        db.setDatabaseName(destinationExisted ? destinationPath : stagedDestination);
        if (!db.open()) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        if (errorKey.isEmpty() && destinationExisted) {
            const auto tables = db.tables();
            if (!tables.contains(QStringLiteral("meta"))
                || !tables.contains(QStringLiteral("folders"))
                || !tables.contains(QStringLiteral("members")))
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        }
        if (errorKey.isEmpty() && !destinationExisted) {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY,value TEXT NOT NULL)"))
                || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS folders "
                    "(legacy_id INTEGER PRIMARY KEY,title TEXT NOT NULL)"))
                || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS members "
                    "(folder_id INTEGER NOT NULL,legacy_song_id INTEGER NOT NULL,title TEXT NOT NULL,"
                    "artist TEXT NOT NULL,duration_seconds INTEGER NOT NULL,path_digest TEXT NOT NULL,"
                    "status TEXT NOT NULL,ref_json TEXT NOT NULL,PRIMARY KEY(folder_id,legacy_song_id))")))
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        }
        QHash<QString, QPair<QString, QVariantMap>> previous;
        if (errorKey.isEmpty()) {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("SELECT value FROM meta WHERE key='schemaVersion'")))
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            else if (query.next()) {
                if (query.value(0).toString() != QLatin1String("1"))
                    errorKey = QStringLiteral("local.collectionMigration.schemaUnsupported");
            } else if (destinationExisted)
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        }
        if (errorKey.isEmpty()) {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("SELECT value FROM meta WHERE key='sourceDatabase'")))
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            else if (query.next()) {
                if (query.value(0).toString() != sourceKey)
                    errorKey = QStringLiteral("local.collectionMigration.sourceMismatch");
            } else if (destinationExisted)
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            if (errorKey.isEmpty() && query.exec(QStringLiteral(
                "SELECT folder_id,legacy_song_id,path_digest,ref_json FROM members WHERE status='matched'"))) {
                while (query.next()) {
                    const auto map = QJsonDocument::fromJson(query.value(3).toByteArray()).object().toVariantMap();
                    if (validRef(map)) previous.insert(QString::number(query.value(0).toLongLong())
                        + ':' + QString::number(query.value(1).toLongLong()),
                        {query.value(2).toString(), map});
                }
                if (query.lastError().isValid())
                    errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            } else if (errorKey.isEmpty()) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        }
        if (errorKey.isEmpty() && !db.transaction())
            errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        if (errorKey.isEmpty()) {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("DELETE FROM members"))
                || !query.exec(QStringLiteral("DELETE FROM folders"))
                || !query.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO meta(key,value) VALUES('sourceDatabase',?)")))
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            if (errorKey.isEmpty()) {
                query.addBindValue(sourceKey);
                if (!query.exec()) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            }
            if (errorKey.isEmpty() && !query.exec(QStringLiteral(
                "INSERT OR REPLACE INTO meta(key,value) VALUES('schemaVersion','1')")))
                errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
        }
        if (errorKey.isEmpty()) {
            QSqlQuery folderQuery(db);
            QSqlQuery memberQuery(db);
            const bool prepared = folderQuery.prepare(QStringLiteral("INSERT INTO folders(legacy_id,title) VALUES(?,?)"))
                && memberQuery.prepare(QStringLiteral("INSERT INTO members "
                    "(folder_id,legacy_song_id,title,artist,duration_seconds,path_digest,status,ref_json) "
                    "VALUES(?,?,?,?,?,?,?,?)"));
            if (!prepared) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            for (const auto &folder : folders) {
                if (!errorKey.isEmpty()) break;
                folderQuery.bindValue(0, folder.id);
                folderQuery.bindValue(1, folder.title);
                if (!folderQuery.exec()) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            }
            for (const auto &song : songs) {
                if (!errorKey.isEmpty()) break;
                const QString key = QString::number(song.folderId) + ':' + QString::number(song.id);
                const QString pathDigest = digest(song.path);
                QVariantMap ref;
                QString status;
                const auto prior = previous.constFind(key);
                if (prior != previous.cend() && prior->first == pathDigest) {
                    ref = prior->second;
                    status = QStringLiteral("matched");
                } else {
                    const QUrl url = legacyUrl(song.path);
                    const auto claimed = url.isValid() && url.isLocalFile()
                        ? m_resolver->resolve(url, candidateInstanceIds)
                        : LegacyMediaIdentityResolver::Result{};
                    status = statusName(claimed.status);
                    if (claimed.status == LegacyMediaIdentityResolver::Status::Matched)
                        ref = mediaRefV2ToVariantMap(claimed.ref);
                }
                if (status == QLatin1String("matched")) ++result.matched;
                else if (status == QLatin1String("noMatch")) ++result.noMatch;
                else if (status == QLatin1String("ambiguous")) ++result.ambiguous;
                else if (status == QLatin1String("unavailable")) ++result.unavailable;
                else ++result.invalidPath;
                memberQuery.bindValue(0, song.folderId);
                memberQuery.bindValue(1, song.id);
                memberQuery.bindValue(2, song.title);
                memberQuery.bindValue(3, song.artist);
                memberQuery.bindValue(4, song.duration);
                memberQuery.bindValue(5, pathDigest);
                memberQuery.bindValue(6, status);
                memberQuery.bindValue(7, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(ref))
                    .toJson(QJsonDocument::Compact)));
                if (!memberQuery.exec()) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
            }
        }
        if (errorKey.isEmpty() && db.commit()) committed = true;
        else db.rollback();
        db.close();
    }
    QSqlDatabase::removeDatabase(destinationConnection);
    if (committed && !destinationExisted) {
        committed = QFile::rename(stagedDestination, destinationPath);
        if (!committed) errorKey = QStringLiteral("local.collectionMigration.destinationUnavailable");
    }
    if (!destinationExisted && !committed) QFile::remove(stagedDestination);
    result.committed = committed;
    if (!committed) result.errorKey = errorKey.isEmpty()
        ? QStringLiteral("local.collectionMigration.destinationUnavailable") : errorKey;
    return result;
}
