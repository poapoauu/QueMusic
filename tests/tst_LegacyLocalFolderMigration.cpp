#include "core/migration/LegacyLocalFolderMigration.h"
#include "SourceAccountStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

namespace {
SettingsSchemaV2 schema()
{
    SettingsFieldV2 root{"rootDirectory", "rootDirectory", SettingsFieldTypeV2::Directory};
    root.required = true;
    SettingsFieldV2 recursive{"recursive", "recursive", SettingsFieldTypeV2::Boolean};
    recursive.defaultValue = true;
    SettingsFieldV2 onOpen{"scanOnOpen", "scanOnOpen", SettingsFieldTypeV2::Boolean};
    onOpen.defaultValue = true;
    SettingsFieldV2 watch{"watchChanges", "watchChanges", SettingsFieldTypeV2::Boolean};
    watch.defaultValue = false;
    SettingsFieldV2 ignore{"ignoreDirectories", "ignoreDirectories", SettingsFieldTypeV2::Text};
    ignore.defaultValue = QString();
    return {{"library", "library", {root, recursive, onOpen, watch, ignore}, {}}};
}
bool database(const QString &path, const QList<QVariantList> &rows)
{
    const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool okay = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            okay = query.exec("CREATE TABLE folders (id INTEGER PRIMARY KEY, name TEXT, type TEXT, path TEXT)");
            for (const auto &row : rows) {
                query.prepare("INSERT INTO folders(id,name,type,path) VALUES(?,?,?,?)");
                for (int i = 0; i < 4; ++i) query.bindValue(i, row.at(i));
                okay = okay && query.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return okay;
}
struct Harness {
    QTemporaryDir files;
    QSettings settings{files.filePath("accounts.ini"), QSettings::IniFormat};
    UnavailableSecretStore secrets;
    SourceAccountStore accounts{&settings, &secrets};
    QString db() const { return files.filePath("player_data.db"); }
};
}

class LegacyLocalFolderMigrationTest final : public QObject {
    Q_OBJECT
private slots:
    void migratesLocalOnlyAndKeepsCollections()
    {
        Harness h;
        QVERIFY(h.files.isValid());
        QVERIFY(QDir(h.files.path()).mkdir("music"));
        const QString music = h.files.filePath("music");
        const QString missing = h.files.filePath("missing");
        QVERIFY(database(h.db(), {{1, "One", "local", music},
                                  {2, "Missing", "local", missing},
                                  {3, "Collection", "my", ""},
                                  {4, "Bad", "local", "relative/music"}}));
        LegacyLocalFolderMigration migrator(&h.accounts, &h.settings);
        const auto first = migrator.run(h.db(), schema());
        QCOMPARE(first.imported, 2);
        QVERIFY(!first.complete);
        QVERIFY(!first.errorKeys.isEmpty());
        QCOMPARE(h.accounts.accounts().size(), 2);
        for (const auto &account : h.accounts.accounts()) {
            QCOMPARE(account.sourceId, QString("local"));
            QVERIFY(account.accountId != "default");
            QVERIFY(account.displayName == "One" || account.displayName == "Missing");
            const auto root = account.parameters.value("rootDirectory").toString();
            QVERIFY(root == QFileInfo(music).canonicalFilePath() || root == missing);
        }
        const auto second = migrator.run(h.db(), schema());
        QCOMPARE(second.imported, 0);
        QCOMPARE(h.accounts.accounts().size(), 2);
        QVERIFY(QFileInfo(h.db()).exists());
        const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(h.db());
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT COUNT(*) FROM folders"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 4);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void pendingWriteRetryAndCommittedDeletion()
    {
        Harness h;
        QVERIFY(h.files.isValid());
        const QString firstRoot = h.files.filePath("one");
        const QString secondRoot = h.files.filePath("two");
        QVERIFY(QDir().mkpath(firstRoot));
        QVERIFY(QDir().mkpath(secondRoot));
        QVERIFY(database(h.db(), {{10, "One", "local", firstRoot},
                                  {20, "Two", "local", secondRoot}}));
        LegacyLocalFolderMigration::Hooks failSave;
        failSave.saveAccount = [](SourceAccountStore *, const SourceAccountSaveV2 &) {
            return false;
        };
        LegacyLocalFolderMigration failing(&h.accounts, &h.settings, failSave);
        const auto failed = failing.run(h.db(), schema());
        QCOMPARE(failed.imported, 0);
        QVERIFY(!failed.complete);
        QCOMPARE(h.accounts.accounts().size(), 0);
        LegacyLocalFolderMigration::Hooks crashAfterSave;
        crashAfterSave.afterAccountSaved = [] { return false; };
        LegacyLocalFolderMigration interrupted(&h.accounts, &h.settings, crashAfterSave);
        const auto interruptedResult = interrupted.run(h.db(), schema());
        QVERIFY(!interruptedResult.complete);
        QVERIFY(!h.accounts.accounts().isEmpty());
        const int savedCount = h.accounts.accounts().size();
        LegacyLocalFolderMigration resumed(&h.accounts, &h.settings);
        const auto resumedResult = resumed.run(h.db(), schema());
        QVERIFY(resumedResult.complete);
        QCOMPARE(h.accounts.accounts().size(), 2);
        QVERIFY(resumedResult.imported <= 2 - savedCount);
        const auto account = h.accounts.accounts().first();
        QVERIFY(h.accounts.remove("local", account.accountId));
        const auto afterDeletion = resumed.run(h.db(), schema());
        QVERIFY(afterDeletion.complete);
        QCOMPARE(h.accounts.accounts().size(), 1);

        // A failed journal sync must not claim a completed migration.
        QTemporaryDir separate;
        QVERIFY(separate.isValid());
        const QString otherDb = separate.filePath("player_data.db");
        QVERIFY(database(otherDb, {{1, "Other", "local", firstRoot}}));
        LegacyLocalFolderMigration::Hooks failSync;
        failSync.syncJournal = [](QSettings *) { return false; };
        LegacyLocalFolderMigration syncFailure(&h.accounts, &h.settings, failSync);
        const auto journalResult = syncFailure.run(otherDb, schema());
        QVERIFY(!journalResult.complete);
        QCOMPARE(h.accounts.accounts().size(), 1);
    }

    void completionSyncFailureIsNotSuccess()
    {
        Harness h;
        QVERIFY(h.files.isValid());
        const QString root = h.files.filePath("music");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(database(h.db(), {{1, "One", "local", root}}));
        LegacyLocalFolderMigration::Hooks hooks;
        hooks.syncJournal = [](QSettings *settings) {
            const auto keys = settings->allKeys();
            if (std::any_of(keys.cbegin(), keys.cend(), [](const QString &key) {
                    return key.endsWith("/complete");
                })) return false;
            settings->sync();
            return settings->status() == QSettings::NoError;
        };
        LegacyLocalFolderMigration migration(&h.accounts, &h.settings, hooks);
        const auto first = migration.run(h.db(), schema());
        QVERIFY(!first.complete);
        QCOMPARE(h.accounts.accounts().size(), 1);
        const auto second = migration.run(h.db(), schema());
        QVERIFY(!second.complete);
        QCOMPARE(h.accounts.accounts().size(), 1);
        QSettings reopened(h.settings.fileName(), QSettings::IniFormat);
        SourceAccountStore freshAccounts(&reopened, &h.secrets);
        LegacyLocalFolderMigration fresh(&freshAccounts, &reopened);
        QVERIFY(fresh.run(h.db(), schema()).complete);
        QCOMPARE(freshAccounts.accounts().size(), 1);
    }

    void pendingSyncFailureMustBeDurableBeforeAccountSave()
    {
        Harness h;
        QVERIFY(h.files.isValid());
        const QString root = h.files.filePath("music");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(database(h.db(), {{1, "One", "local", root}}));
        LegacyLocalFolderMigration::Hooks failPending;
        failPending.syncJournal = [](QSettings *) { return false; };
        const auto failed = LegacyLocalFolderMigration(&h.accounts, &h.settings, failPending)
                                .run(h.db(), schema());
        QVERIFY(!failed.complete);
        QCOMPARE(h.accounts.accounts().size(), 0);

        bool savedAfterDurablePending = false;
        LegacyLocalFolderMigration::Hooks retry;
        retry.saveAccount = [&](SourceAccountStore *accounts,
                                const SourceAccountSaveV2 &request) {
            QFile persisted(h.settings.fileName());
            savedAfterDurablePending = persisted.open(QIODevice::ReadOnly)
                && persisted.readAll().contains("pending");
            return savedAfterDurablePending && accounts->saveValidatedV2(request);
        };
        const auto result = LegacyLocalFolderMigration(&h.accounts, &h.settings, retry)
                                .run(h.db(), schema());
        QVERIFY(result.complete);
        QVERIFY(savedAfterDurablePending);
        QCOMPARE(h.accounts.accounts().size(), 1);
    }

    void restartAdoptsPendingAndPreservesDeletionTombstone()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString root = files.filePath("music");
        QVERIFY(QDir().mkpath(root));
        const QString db = files.filePath("player_data.db");
        const QString ini = files.filePath("accounts.ini");
        QVERIFY(database(db, {{1, "One", "local", root}}));
        UnavailableSecretStore secrets;
        {
            QSettings journal(ini, QSettings::IniFormat);
            SourceAccountStore store(&journal, &secrets);
            LegacyLocalFolderMigration::Hooks fail;
            fail.saveAccount = [](SourceAccountStore *, const SourceAccountSaveV2 &) { return false; };
            QVERIFY(!LegacyLocalFolderMigration(&store, &journal, fail).run(db, schema()).complete);
            QCOMPARE(store.accounts().size(), 0);
        }
        {
            QSettings journal(ini, QSettings::IniFormat);
            SourceAccountStore store(&journal, &secrets);
            LegacyLocalFolderMigration::Hooks interrupt;
            interrupt.afterAccountSaved = [] { return false; };
            QVERIFY(!LegacyLocalFolderMigration(&store, &journal, interrupt).run(db, schema()).complete);
            QCOMPARE(store.accounts().size(), 1);
        }
        {
            QSettings journal(ini, QSettings::IniFormat);
            SourceAccountStore store(&journal, &secrets);
            auto result = LegacyLocalFolderMigration(&store, &journal).run(db, schema());
            QVERIFY(result.complete);
            QCOMPARE(result.imported, 0); // adopt the persisted pending account
            QCOMPARE(store.accounts().size(), 1);
            QVERIFY(store.remove("local", store.accounts().first().accountId));
        }
        {
            QSettings journal(ini, QSettings::IniFormat);
            SourceAccountStore store(&journal, &secrets);
            QVERIFY(LegacyLocalFolderMigration(&store, &journal).run(db, schema()).complete);
            QCOMPARE(store.accounts().size(), 0);
        }
    }

    void committedDeletionIsNotRevivedWhileAnotherRowRetries()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString root = files.filePath("music");
        QVERIFY(QDir().mkpath(root));
        const QString db = files.filePath("player_data.db");
        const QString ini = files.filePath("accounts.ini");
        QVERIFY(database(db, {{1, "One", "local", root}, {2, "Two", "local", root}}));
        UnavailableSecretStore secrets;
        {
            QSettings journal(ini, QSettings::IniFormat);
            SourceAccountStore store(&journal, &secrets);
            LegacyLocalFolderMigration::Hooks failSecond;
            failSecond.saveAccount = [](SourceAccountStore *accounts,
                                        const SourceAccountSaveV2 &request) {
                return request.displayName == "One" && accounts->saveValidatedV2(request);
            };
            auto result = LegacyLocalFolderMigration(&store, &journal, failSecond).run(db, schema());
            QVERIFY(!result.complete);
            QCOMPARE(store.accounts().size(), 1);
            QVERIFY(store.remove("local", store.accounts().first().accountId));
        }
        {
            QSettings journal(ini, QSettings::IniFormat);
            SourceAccountStore store(&journal, &secrets);
            auto result = LegacyLocalFolderMigration(&store, &journal).run(db, schema());
            QVERIFY(result.complete);
            QCOMPARE(result.imported, 1);
            QCOMPARE(store.accounts().size(), 1);
            QCOMPARE(store.accounts().first().displayName, QString("Two"));
        }
    }
};
QTEST_MAIN(LegacyLocalFolderMigrationTest)
#include "tst_LegacyLocalFolderMigration.moc"
