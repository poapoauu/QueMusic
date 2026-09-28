#include "LegacyLocalFolderMigration.h"
#include "SourceAccountStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <utility>

namespace {
const QUuid migrationNamespace(QStringLiteral("{eecbfc74-7c5a-5a32-9781-8eca52ba5970}"));

QString canonicalDatabasePath(const QString &path)
{
    const QFileInfo info(path);
    return info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                              : info.canonicalFilePath();
}

QString migrationRoot(const QString &databasePath)
{
    const QByteArray digest = QCryptographicHash::hash(
        canonicalDatabasePath(databasePath).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("migrations/localFolders/v1/") + QString::fromLatin1(digest);
}

QString accountIdFor(const QString &databasePath, qlonglong rowId)
{
    const QByteArray name = canonicalDatabasePath(databasePath).toUtf8()
        + '/' + QByteArray::number(rowId);
    return QUuid::createUuidV5(migrationNamespace, name).toString(QUuid::WithoutBraces);
}

QString localRoot(const QString &legacyPath)
{
    if (legacyPath.isEmpty() || !QDir::isAbsolutePath(legacyPath)
        || legacyPath.startsWith("//") || legacyPath.startsWith("\\\\")
        || legacyPath.contains("://")) return {};
    const QString cleaned = QDir::cleanPath(legacyPath);
    const QFileInfo info(cleaned);
    if (info.exists() && !info.isDir()) return {};
    return info.canonicalFilePath().isEmpty() ? cleaned : info.canonicalFilePath();
}
}

LegacyLocalFolderMigration::LegacyLocalFolderMigration(SourceAccountStore *accounts,
                                                       QSettings *journal, Hooks hooks)
    : m_accounts(accounts), m_journal(journal), m_hooks(std::move(hooks)) {}

LegacyLocalMigrationResult LegacyLocalFolderMigration::run(
    const QString &databasePath, const SettingsSchemaV2 &schema)
{
    LegacyLocalMigrationResult result;
    if (!m_accounts || !m_journal || schema.isEmpty()) {
        result.errorKeys.append(QStringLiteral("local.migration.unavailable"));
        return result;
    }
    const auto sync = [this] {
        if (m_hooks.syncJournal) return m_hooks.syncJournal(m_journal);
        m_journal->sync();
        return m_journal->status() == QSettings::NoError;
    };
    // The account store may share this QSettings object. Never enter a group
    // across a saveValidatedV2 call or its account keys become nested here.
    const QString prefix = migrationRoot(databasePath) + '/';
    if (m_journal->value(prefix + "complete").toBool()) {
        result.complete = true;
        return result;
    }
    if (!QFileInfo::exists(databasePath)) {
        // No old installation data. This is a no-op, not a reason to create a DB.
        result.complete = true;
        return result;
    }

    const QString connection = QStringLiteral("local_migration_")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    struct Row { qlonglong id; QString name; QString path; };
    QList<Row> rows;
    bool databaseOkay = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(databasePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            if (!db.tables().contains(QStringLiteral("folders"))) {
                databaseOkay = true;
            } else {
                QSqlQuery query(db);
                databaseOkay = query.exec(QStringLiteral(
                    "SELECT id, name, path FROM folders WHERE type='local' ORDER BY id"));
                if (databaseOkay) {
                    while (query.next()) rows.append({query.value(0).toLongLong(),
                                                      query.value(1).toString(),
                                                      query.value(2).toString()});
                    databaseOkay = !query.lastError().isValid();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (!databaseOkay) {
        result.errorKeys.append(QStringLiteral("local.migration.databaseUnavailable"));
        return result;
    }

    for (const Row &row : rows) {
        const QString key = prefix + QStringLiteral("rows/") + QString::number(row.id);
        const QString state = m_journal->value(key + "/state").toString();
        const QString mappedId = accountIdFor(databasePath, row.id);
        const QString journalId = m_journal->value(key + "/accountId").toString();
        if (state == QLatin1String("committed")) {
            // A missing committed account is a user-deletion tombstone.
            ++result.skipped;
            continue;
        }
        if (!state.isEmpty() && (state != QLatin1String("pending") || journalId != mappedId)) {
            result.errorKeys.append(QStringLiteral("local.migration.journalInvalid"));
            continue;
        }
        const QString root = localRoot(row.path);
        if (root.isEmpty()) {
            result.errorKeys.append(QStringLiteral("local.migration.invalidPath"));
            continue;
        }
        if (state.isEmpty()) {
            m_journal->setValue(key + "/accountId", mappedId);
            m_journal->setValue(key + "/state", QStringLiteral("pending"));
            if (!sync()) {
                // A failed write must not be treated as a durable pending record
                // by a retry using this same QSettings object.
                m_journal->remove(key + "/state");
                m_journal->remove(key + "/accountId");
                result.errorKeys.append(QStringLiteral("local.migration.journalWriteFailed"));
                continue;
            }
        }
        const auto existing = m_accounts->storedAccount(QStringLiteral("local"), mappedId);
        bool savedNow = false;
        if (!existing) {
            SourceAccountSaveV2 request;
            request.pluginPackageId = QStringLiteral("org.quemusic.source.local");
            request.sourceId = QStringLiteral("local");
            request.accountId = mappedId;
            request.displayName = row.name;
            request.schema = schema;
            request.draft.insert(QStringLiteral("rootDirectory"), root);
            const bool saved = m_hooks.saveAccount
                ? m_hooks.saveAccount(m_accounts, request)
                : m_accounts->saveValidatedV2(request);
            if (!saved) {
                result.errorKeys.append(QStringLiteral("local.migration.accountWriteFailed"));
                continue;
            }
            savedNow = true;
            if (m_hooks.afterAccountSaved && !m_hooks.afterAccountSaved()) {
                result.errorKeys.append(QStringLiteral("local.migration.interrupted"));
                continue;
            }
        }
        m_journal->setValue(key + "/state", QStringLiteral("committed"));
        if (!sync()) {
            // The durable state may still be pending. Keep retry semantics
            // consistent even when the process does not restart.
            m_journal->setValue(key + "/state", QStringLiteral("pending"));
            result.errorKeys.append(QStringLiteral("local.migration.journalWriteFailed"));
            continue;
        }
        if (savedNow) ++result.imported;
        else ++result.skipped;
    }
    if (!result.errorKeys.isEmpty()) {
        result.errorKeys.removeDuplicates();
        return result;
    }
    m_journal->setValue(prefix + "complete", true);
    if (!sync()) {
        // Do not let an unflushed in-memory marker masquerade as success on
        // another run with this same QSettings instance.
        m_journal->remove(prefix + "complete");
        result.errorKeys.append(QStringLiteral("local.migration.journalWriteFailed"));
        return result;
    }
    result.complete = true;
    return result;
}
