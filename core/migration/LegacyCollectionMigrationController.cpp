#include "LegacyCollectionMigrationController.h"

#include "SourceRegistry.h"
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

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
    return {{QStringLiteral("committed"), result.committed},
            {QStringLiteral("errorKey"), result.errorKey},
            {QStringLiteral("matched"), result.matched},
            {QStringLiteral("noMatch"), result.noMatch},
            {QStringLiteral("ambiguous"), result.ambiguous},
            {QStringLiteral("unavailable"), result.unavailable},
            {QStringLiteral("invalidPath"), result.invalidPath}};
}
