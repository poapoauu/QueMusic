#include "PlaybackCoordinator.h"
#include "LegacyMediaIdentityResolver.h"
#include "LegacyCollectionMigration.h"
#include "LegacyCollectionMigrationController.h"
#include "PlaybackSink.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include "extensions/legacy-identity/v1/ILegacyMediaIdentityProviderV1.h"
#include "extensions/item-lookup/v1/IItemLookupProviderV1.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QTest>

namespace {
const QString packageId = QStringLiteral("org.quemusic.source.local");
bool writeAudio(const QString &path)
{
    auto le = [](quint32 value, int count) {
        QByteArray bytes;
        for (int i = 0; i < count; ++i) bytes.append(char((value >> (8 * i)) & 255));
        return bytes;
    };
    const QByteArray wave = QByteArray("RIFF", 4) + le(36 + 16000, 4) + "WAVEfmt "
        + le(16, 4) + le(1, 2) + le(1, 2) + le(8000, 4) + le(16000, 4)
        + le(2, 2) + le(16, 2) + "data" + le(16000, 4) + QByteArray(16000, '\0');
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(wave) == wave.size();
}
struct Sink final : PlaybackSink {
    int prepares = 0;
    int plays = 0;
    int stops = 0;
    StreamDescriptorV2 stream;
    bool prepare(StreamDescriptorV2 next, QUuid) override
    {
        ++prepares;
        stream = std::move(next);
        return true;
    }
    void play(QUuid) override { ++plays; }
    void stop(QUuid) override { ++stops; }
};
struct Harness {
    QTemporaryDir storage;
    QSettings settings{storage.filePath("accounts.ini"), QSettings::IniFormat};
    UnavailableSecretStore secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    Sink sink;
    PlaybackCoordinator coordinator{&registry, &sink};
    bool load()
    {
        plugins.addSearchPath(QStringLiteral(QUEMUSIC_LOCAL_PACKAGE_ROOT));
        plugins.discover();
        return plugins.load(packageId);
    }
    bool save(const QString &account, const QString &root, bool enabled = true)
    {
        auto *schema = qobject_cast<IPluginSettingsProviderV2 *>(plugins.pluginInstance(packageId));
        if (!schema) return false;
        SourceAccountSaveV2 request;
        request.pluginPackageId = packageId;
        request.sourceId = QStringLiteral("local");
        request.accountId = account;
        request.displayName = QStringLiteral("Library ") + account;
        request.enabled = enabled;
        request.schema = schema->settingsSchema();
        request.draft.insert("rootDirectory", root);
        request.draft.insert("scanOnOpen", false);
        return accounts.saveValidatedV2(request);
    }
    PageQueryV2 children(const QString &account, const QString &root)
    {
        Q_UNUSED(root);
        PageQueryV2 query;
        query.page = MusicPageKindV2::Category;
        query.section = PageSectionKindV2::Tracks;
        query.scope.sourceInstanceId = QStringLiteral("local/") + account;
        auto *session = registry.sessionFor(query.scope.sourceInstanceId);
        if (!session) return query;
        QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
        PageQueryV2 rootQuery = query;
        rootQuery.filters.insert("entityType", int(MediaEntityTypeV2::Directory));
        qobject_cast<IPageProviderV2 *>(session)->fetchPage(rootQuery);
        if (ready.isEmpty() && !ready.wait(5000)) return query;
        const auto result = qvariant_cast<PageResultV2>(ready.last().at(1));
        query.filters.insert("directoryId", result.sections.first().items.first().ref.entityId);
        return query;
    }
};
QVariantMap playbackRow(const MediaItemV2 &item)
{
    QVariantMap actions;
    actions.insert(QString::number(int(SourceActionV2::Play)),
                   QVariantMap{{"state", int(AvailabilityV2::Available)},
                               {"constraints", QVariantMap{}}});
    return {{"ref", mediaRefV2ToVariantMap(item.ref)},
            {"availableActions", actions}, {"title", item.title},
            {"durationMs", item.durationMs}};
}
}

class LocalSourceIntegrationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void dynamicInstancesUseCoordinator()
    {
        Harness h;
        QVERIFY(h.load());
        QCOMPARE(h.plugins.plugin(packageId).state, PluginState::Loaded);
        QCOMPARE(h.registry.enabledInstances().size(), 0);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString track = root.filePath("song.wav");
        QVERIFY(writeAudio(track));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));
        auto *first = h.registry.sessionFor("local/one");
        auto *second = h.registry.sessionFor("local/two");
        QVERIFY(first && second && first != second);
        QTRY_COMPARE(first->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(second->state(), SourceSessionStateV2::Ready);
        auto *firstPage = qobject_cast<IPageProviderV2 *>(first);
        auto *secondPage = qobject_cast<IPageProviderV2 *>(second);
        QVERIFY(firstPage && secondPage);
        QSignalSpy oneReady(first, &IMusicSourceSessionV2::pageReady);
        QSignalSpy twoReady(second, &IMusicSourceSessionV2::pageReady);
        firstPage->fetchPage(h.children("one", root.path()));
        secondPage->fetchPage(h.children("two", root.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!oneReady.isEmpty() && !twoReady.isEmpty(), 5000);
        const auto firstItem = qvariant_cast<PageResultV2>(oneReady.last().at(1)).sections.first().items.first();
        const auto secondItem = qvariant_cast<PageResultV2>(twoReady.last().at(1)).sections.first().items.first();
        QCOMPARE(firstItem.ref.sourcePluginId, QString("local"));
        QCOMPARE(firstItem.ref.sourceInstanceId, QString("local/one"));
        QCOMPARE(secondItem.ref.sourceInstanceId, QString("local/two"));
        QVERIFY(!QUuid(firstItem.ref.entityId).isNull());
        QVERIFY(!QUuid(secondItem.ref.entityId).isNull());
        QVERIFY(firstItem.ref.entityId != secondItem.ref.entityId);
        QVERIFY(firstItem.ref != secondItem.ref);
        QSignalSpy failed(&h.coordinator, &PlaybackCoordinator::playbackFailed);
        QVERIFY(!h.coordinator.play(playbackRow(firstItem)).isNull());
        QTRY_COMPARE(h.sink.plays, 1);
        QCOMPARE(h.sink.stream.url.toLocalFile(), QFileInfo(track).canonicalFilePath());
        QCOMPARE(h.sink.stream.media, firstItem.ref);
        QVERIFY(h.sink.stream.headers.isEmpty());
        QVERIFY(QFile::remove(track));
        h.coordinator.play(playbackRow(firstItem));
        QTRY_VERIFY(!failed.isEmpty());
        QCOMPARE(h.sink.plays, 1);
        QVERIFY(h.registry.disableInstance("local/one"));
        h.coordinator.play(playbackRow(firstItem));
        QTRY_VERIFY(failed.size() >= 2);
        QCOMPARE(h.sink.plays, 1);
    }

    void switchingInstancesAndDisablingCurrentStopsPlayback()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeAudio(root.filePath("song.wav")));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));

        auto trackFor = [&](const QString &account) {
            auto *session = h.registry.sessionFor(QStringLiteral("local/") + account);
            if (!session || session->state() != SourceSessionStateV2::Ready)
                return MediaItemV2{};
            const auto query = h.children(account, root.path());
            QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
            qobject_cast<IPageProviderV2 *>(session)->fetchPage(query);
            if (ready.isEmpty() && !ready.wait(5000))
                return MediaItemV2{};
            const auto result = qvariant_cast<PageResultV2>(ready.last().at(1));
            return result.sections.first().items.first();
        };
        auto *firstSession = h.registry.sessionFor("local/one");
        auto *secondSession = h.registry.sessionFor("local/two");
        QVERIFY(firstSession && secondSession);
        QTRY_COMPARE(firstSession->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(secondSession->state(), SourceSessionStateV2::Ready);
        const auto first = trackFor("one");
        const auto second = trackFor("two");
        QVERIFY(!first.ref.entityId.isEmpty());
        QVERIFY(!second.ref.entityId.isEmpty());

        const auto firstGeneration = h.coordinator.play(playbackRow(first));
        QVERIFY(!firstGeneration.isNull());
        QTRY_COMPARE(h.sink.plays, 1);
        QCOMPARE(h.sink.stream.media, first.ref);
        const auto secondGeneration = h.coordinator.play(playbackRow(second));
        QVERIFY(!secondGeneration.isNull());
        QVERIFY(secondGeneration != firstGeneration);
        QTRY_COMPARE(h.sink.plays, 2);
        QCOMPARE(h.sink.stream.media, second.ref);
        QCOMPARE(h.sink.stream.url.scheme(), QStringLiteral("file"));
        QCOMPARE(h.sink.stops, 1);

        QVERIFY(h.registry.disableInstance("local/one"));
        QCOMPARE(h.coordinator.currentGeneration(), secondGeneration);
        QCOMPARE(h.sink.stops, 1);
        QVERIFY(h.registry.disableInstance("local/two"));
        QTRY_VERIFY(h.coordinator.currentGeneration().isNull());
        QCOMPARE(h.sink.stops, 2);
    }

    void legacyFileClaimOnlyReturnsIndexedPluginIdentity()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid() && outside.isValid());
        const QString track = root.filePath("song.wav");
        const QString foreign = outside.filePath("foreign.wav");
        QVERIFY(writeAudio(track));
        QVERIFY(writeAudio(foreign));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));
        auto *first = h.registry.sessionFor("local/one");
        auto *second = h.registry.sessionFor("local/two");
        QVERIFY(first && second);
        QTRY_COMPARE(first->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(second->state(), SourceSessionStateV2::Ready);
        auto *firstClaims = qobject_cast<ILegacyMediaIdentityProviderV1 *>(first);
        auto *secondClaims = qobject_cast<ILegacyMediaIdentityProviderV1 *>(second);
        QVERIFY(firstClaims && secondClaims);
        auto *firstLookup = qobject_cast<IItemLookupProviderV1 *>(first);
        auto *secondLookup = qobject_cast<IItemLookupProviderV1 *>(second);
        QVERIFY(firstLookup && secondLookup);
        const QUrl fileUrl = QUrl::fromLocalFile(track);
        QVERIFY(!firstClaims->claimLegacyFile(fileUrl)); // No scanned identity yet.
        QVERIFY(!firstLookup->lookupItem({QStringLiteral("local"), QStringLiteral("local/one"),
                                         QStringLiteral("one"), MediaEntityTypeV2::Track,
                                         QStringLiteral("not-scanned")}));
        const auto firstQuery = h.children("one", root.path());
        const auto secondQuery = h.children("two", root.path());
        QSignalSpy firstReady(first, &IMusicSourceSessionV2::pageReady);
        QSignalSpy secondReady(second, &IMusicSourceSessionV2::pageReady);
        qobject_cast<IPageProviderV2 *>(first)->fetchPage(firstQuery);
        qobject_cast<IPageProviderV2 *>(second)->fetchPage(secondQuery);
        QTRY_VERIFY_WITH_TIMEOUT(!firstReady.isEmpty() && !secondReady.isEmpty(), 5000);
        const auto firstItem = qvariant_cast<PageResultV2>(firstReady.last().at(1)).sections.first().items.first();
        const auto secondItem = qvariant_cast<PageResultV2>(secondReady.last().at(1)).sections.first().items.first();
        QVERIFY(!firstItem.ref.entityId.isEmpty());
        QVERIFY(!secondItem.ref.entityId.isEmpty());
        const auto firstRef = firstClaims->claimLegacyFile(fileUrl);
        const auto secondRef = secondClaims->claimLegacyFile(fileUrl);
        QVERIFY(firstRef);
        QVERIFY(secondRef);
        QCOMPARE(*firstRef, firstItem.ref);
        QCOMPARE(*secondRef, secondItem.ref);
        QCOMPARE(firstRef->sourceInstanceId, QStringLiteral("local/one"));
        QCOMPARE(secondRef->sourceInstanceId, QStringLiteral("local/two"));
        QVERIFY(firstRef->entityId != secondRef->entityId);
        QVERIFY(!QUuid(firstRef->entityId).isNull());
        QCOMPARE(firstRef->entityType, MediaEntityTypeV2::Track);
        const auto lookedUp = firstLookup->lookupItem(*firstRef);
        QVERIFY(lookedUp);
        QCOMPARE(lookedUp->ref, firstItem.ref);
        QCOMPARE(lookedUp->title, firstItem.title);
        QCOMPARE(lookedUp->availableActions.value(SourceActionV2::Play).state,
                 AvailabilityV2::Available);
        QVERIFY(!secondLookup->lookupItem(*firstRef));
        QVERIFY(!firstLookup->lookupItem({firstRef->sourcePluginId,
            firstRef->sourceInstanceId, firstRef->accountId,
            MediaEntityTypeV2::Track, QStringLiteral("unknown")}));
        QVERIFY(!firstClaims->claimLegacyFile(QUrl::fromLocalFile(foreign)));
        const QString unindexed = root.filePath("unindexed.wav");
        QVERIFY(writeAudio(unindexed));
        QVERIFY(!firstClaims->claimLegacyFile(QUrl::fromLocalFile(unindexed)));
#ifndef Q_OS_WIN
        const QString escape = root.filePath("escape.wav");
        QVERIFY(QFile::link(foreign, escape));
        QVERIFY(!firstClaims->claimLegacyFile(QUrl::fromLocalFile(escape)));
#endif
        QVERIFY(!firstClaims->claimLegacyFile(QUrl("https://example.org/song.wav")));
        QUrl withQuery = fileUrl;
        withQuery.setQuery(QStringLiteral("token=ignored"));
        QVERIFY(!firstClaims->claimLegacyFile(withQuery));
        QVERIFY(QFile::remove(track));
        QVERIFY(!firstClaims->claimLegacyFile(fileUrl));
        QVERIFY(!firstLookup->lookupItem(*firstRef));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(foreign, track));
        QVERIFY(!firstLookup->lookupItem(*firstRef));
#endif
        first->close();
        QVERIFY(!firstClaims->claimLegacyFile(fileUrl));
        QVERIFY(!firstLookup->lookupItem(*firstRef));
    }

    void migrationResolverRequiresOneAvailableOwner()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid() && outside.isValid());
        const QString track = root.filePath("legacy track.wav");
        const QString foreign = outside.filePath("foreign.wav");
        QVERIFY(writeAudio(track));
        QVERIFY(writeAudio(foreign));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));
        auto *one = h.registry.sessionFor("local/one");
        auto *two = h.registry.sessionFor("local/two");
        QVERIFY(one && two);
        QTRY_COMPARE(one->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(two->state(), SourceSessionStateV2::Ready);
        h.children("one", root.path());
        h.children("two", root.path());
        LegacyMediaIdentityResolver resolver(&h.registry);
        using Status = LegacyMediaIdentityResolver::Status;
        const QUrl fileUrl = QUrl::fromLocalFile(track);
        QCOMPARE(resolver.resolve(fileUrl, {}).status, Status::InvalidRequest);
        QCOMPARE(resolver.resolve(QUrl("https://example.org/song"), {"local/one"}).status,
                 Status::InvalidRequest);
        QCOMPARE(resolver.resolve(QUrl::fromLocalFile(foreign), {"local/one", "local/two"}).status,
                 Status::NoMatch);
        const auto unique = resolver.resolve(fileUrl, {"local/one", "local/one"});
        QCOMPARE(unique.status, Status::Matched);
        QCOMPARE(unique.ref.sourceInstanceId, QStringLiteral("local/one"));
        QVERIFY(!QUuid(unique.ref.entityId).isNull());
        QVERIFY(!unique.ref.entityId.contains(track));
        QCOMPARE(resolver.resolve(fileUrl, {"local/one", "local/two"}).status,
                 Status::Ambiguous);
        QCOMPARE(h.plugins.plugin(packageId).activeLeases, 2);

        QVERIFY(h.registry.disableInstance("local/two"));
        QCOMPARE(resolver.resolve(fileUrl, {"local/one", "local/two"}).status,
                 Status::Unavailable);
        QCOMPARE(resolver.resolve(fileUrl, {"local/one"}).status, Status::Matched);
        QVERIFY(QFile::remove(track));
        QCOMPARE(resolver.resolve(fileUrl, {"local/one"}).status, Status::NoMatch);
        QCOMPARE(resolver.resolve(fileUrl, {"local/one", "missing/instance"}).status,
                 Status::Unavailable);
        QObject::connect(&h.plugins, &PluginManager::pluginChanged, &h.registry,
                         [&](const QString &changed) {
                             if (changed == packageId)
                                 h.registry.disableInstance("local/one");
                         }, Qt::SingleShotConnection);
        QCOMPARE(resolver.resolve(fileUrl, {"local/one"}).status, Status::Unavailable);
        QVERIFY(!h.registry.sessionFor("local/one"));
    }

    void legacyCollectionsMigrateWithBackupAndRetry()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid() && outside.isValid());
        const QString track = root.filePath("known.wav");
        const QString foreign = outside.filePath("foreign.wav");
        QVERIFY(writeAudio(track));
        QVERIFY(writeAudio(foreign));
        QVERIFY(h.save("one", root.path()));
        QVERIFY(h.save("two", root.path()));
        auto *one = h.registry.sessionFor("local/one");
        auto *two = h.registry.sessionFor("local/two");
        QVERIFY(one && two);
        QTRY_COMPARE(one->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(two->state(), SourceSessionStateV2::Ready);
        h.children("one", root.path());
        h.children("two", root.path());

        const QString oldDb = h.storage.filePath("old.sqlite");
        const QString newDb = h.storage.filePath("collections.sqlite");
        const QString backup = h.storage.filePath("old.backup.sqlite");
        const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(oldDb);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE folders(id INTEGER PRIMARY KEY,name TEXT,type TEXT)"));
            QVERIFY(query.exec("CREATE TABLE songs(id INTEGER PRIMARY KEY,folder_id INTEGER,name TEXT,"
                               "path TEXT,singer TEXT,duration INTEGER)"));
            QVERIFY(query.exec("INSERT INTO folders VALUES(1,'Favorites','my')"));
            QVERIFY(query.exec("INSERT INTO folders VALUES(2,'Legacy local','local')"));
            query.prepare("INSERT INTO songs VALUES(?,?,?,?,?,?)");
            const QList<QString> paths{track, foreign, QStringLiteral("relative.wav"), track};
            for (int i = 0; i < paths.size(); ++i) {
                query.bindValue(0, i + 1);
                query.bindValue(1, i == 3 ? 2 : 1);
                query.bindValue(2, QStringLiteral("Track %1").arg(i + 1));
                query.bindValue(3, paths[i]);
                query.bindValue(4, QStringLiteral("Artist"));
                query.bindValue(5, 42);
                QVERIFY(query.exec());
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);

        LegacyMediaIdentityResolver resolver(&h.registry);
        LegacyCollectionMigration migration(&resolver);
        LegacyCollectionMigrationController controller(&h.registry, &h.coordinator,
                                                        oldDb, newDb, backup);
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("notMigrated"));
        QCOMPARE(controller.preview().value(QStringLiteral("folderCount")).toInt(), 1);
        QCOMPARE(controller.preview().value(QStringLiteral("songCount")).toInt(), 3);
        QCOMPARE(controller.candidates().size(), 2);
        for (const auto &candidate : controller.candidates()) {
            const auto row = candidate.toMap();
            QVERIFY(row.value(QStringLiteral("displayName")).toString().contains(
                row.value(QStringLiteral("instanceId")).toString()));
        }
        const auto invalidCandidate = controller.run(QStringLiteral("local/not-configured"));
        QVERIFY(!invalidCandidate.value(QStringLiteral("committed")).toBool());
        QCOMPARE(invalidCandidate.value(QStringLiteral("errorKey")).toString(),
                 QStringLiteral("local.collectionMigration.invalidCandidate"));
        QVERIFY(!QFileInfo::exists(newDb));
        QVERIFY(!QFileInfo::exists(backup));
        const QString backupDirectory = h.storage.filePath("backup-directory");
        QVERIFY(QDir().mkpath(backupDirectory));
        const auto noBackup = migration.run(oldDb, newDb, backupDirectory, {"local/one"});
        QVERIFY(!noBackup.committed);
        QCOMPARE(noBackup.errorKey, QStringLiteral("local.collectionMigration.backupFailed"));
        QVERIFY(!QFileInfo::exists(newDb));
        const auto first = migration.run(oldDb, newDb, backup, {"local/one", "local/two"});
        QVERIFY2(first.committed, qPrintable(first.errorKey));
        QCOMPARE(first.ambiguous, 1);
        QCOMPARE(first.noMatch, 1);
        QCOMPARE(first.invalidPath, 1);
        QVERIFY(!controller.playSong(1, 1));
        QVERIFY(!controller.enqueueSong(1, 2));
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("ambiguous"));
        QCOMPARE(controller.songStatus(1, 2), QStringLiteral("noMatch"));
        QCOMPARE(controller.songStatus(1, 3), QStringLiteral("invalidPath"));
        QCOMPARE(controller.songStatus(1, 99), QStringLiteral("pending"));
        QVERIFY(QFileInfo::exists(backup));
        QFile output(newDb);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QByteArray raw = output.readAll();
        QVERIFY(!raw.contains(track.toUtf8()));
        QVERIFY(!raw.contains(foreign.toUtf8()));

        const auto second = controller.run(QStringLiteral("local/one"));
        QVERIFY2(second.value(QStringLiteral("committed")).toBool(),
                 qPrintable(second.value(QStringLiteral("errorKey")).toString()));
        QCOMPARE(second.value(QStringLiteral("matched")).toInt(), 1);
        QCOMPARE(second.value(QStringLiteral("noMatch")).toInt(), 1);
        QCOMPARE(second.value(QStringLiteral("invalidPath")).toInt(), 1);
        QCOMPARE(controller.revision(), quint64(1));
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("matched"));
        QVERIFY(controller.playSong(1, 1));
        QTRY_COMPARE(h.sink.plays, 1);
        QCOMPARE(h.sink.stream.media.sourceInstanceId, QStringLiteral("local/one"));
        QVERIFY(controller.enqueueSong(1, 1));
        QCOMPARE(h.coordinator.queue().size(), 2);
        QVERIFY(!QString::fromUtf8(QJsonDocument::fromVariant(h.coordinator.queue())
            .toJson(QJsonDocument::Compact)).contains(track));
        QVERIFY(QFile::remove(track));
        QVERIFY(QFile::link(foreign, track));
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("unavailable"));
        QVERIFY(!controller.playSong(1, 1));
        QVERIFY(h.registry.disableInstance("local/one"));
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("unavailable"));
        QCOMPARE(controller.candidates().size(), 1);
        QVERIFY(!controller.run(QStringLiteral("local/one"))
                    .value(QStringLiteral("committed")).toBool());
        const auto retry = migration.run(oldDb, newDb, backup, {"local/one"});
        QVERIFY2(retry.committed, qPrintable(retry.errorKey));
        QCOMPARE(retry.matched, 1); // Keep the committed identity when the instance is offline.
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(newDb);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT COUNT(*) FROM members"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 3);
            QVERIFY(query.exec("SELECT status,ref_json FROM members WHERE legacy_song_id=1"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("matched"));
            QVERIFY(query.value(1).toString().contains("local/one"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(oldDb);
            QVERIFY(db.open());
            QSqlQuery query(db);
            query.prepare("UPDATE songs SET path=? WHERE id=1");
            query.addBindValue(QStringLiteral("relative.wav"));
            QVERIFY(query.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("stale"));
        const auto changed = migration.run(oldDb, newDb, backup, {"local/one"});
        QVERIFY2(changed.committed, qPrintable(changed.errorKey));
        QCOMPARE(changed.matched, 0);
        QCOMPARE(changed.invalidPath, 2);
        QCOMPARE(controller.songStatus(1, 1), QStringLiteral("invalidPath"));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(backup);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT path FROM songs WHERE id=1"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), track);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        const QString unrelatedPath = h.storage.filePath("unrelated.sqlite");
        QFile unrelated(unrelatedPath);
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        QCOMPARE(unrelated.write("unrelated user data"), qint64(19));
        unrelated.close();
        const auto rejected = migration.run(oldDb, unrelatedPath, backup, {"local/one"});
        QVERIFY(!rejected.committed);
        QVERIFY(unrelated.open(QIODevice::ReadOnly));
        QCOMPARE(unrelated.readAll(), QByteArray("unrelated user data"));
        unrelated.close();
        const QString lookalikePath = h.storage.filePath("lookalike.sqlite");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(lookalikePath);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)"));
            QVERIFY(query.exec("CREATE TABLE folders(legacy_id INTEGER PRIMARY KEY,title TEXT NOT NULL)"));
            QVERIFY(query.exec("CREATE TABLE members(folder_id INTEGER,legacy_song_id INTEGER)"));
            QVERIFY(query.exec("INSERT INTO folders VALUES(99,'unrelated')"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        const auto lookalike = migration.run(oldDb, lookalikePath, backup, {"local/one"});
        QVERIFY(!lookalike.committed);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(lookalikePath);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT title FROM folders WHERE legacy_id=99"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("unrelated"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void unloadWaitsForWorkersAndLease()
    {
        Harness h;
        QVERIFY(h.load());
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeAudio(root.filePath("song.wav")));
        QVERIFY(h.save("disabled", root.path(), false));
        QCOMPARE(h.registry.enabledInstances().size(), 1);
        QVERIFY(!h.registry.enabledInstances().first().enabled);
        QVERIFY(!h.registry.sessionFor("local/disabled"));
        QVERIFY(h.save("active", root.path()));
        auto *session = h.registry.sessionFor("local/active");
        QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        QSignalSpy changed(&h.registry, &SourceRegistry::instanceContentChanged);
        qobject_cast<IPageProviderV2 *>(session)->fetchPage(h.children("active", root.path()));
        QCoreApplication::processEvents();
        QCOMPARE(h.plugins.unload(packageId), PluginOperationResult::Busy);
        QVERIFY(h.registry.disableInstance("local/active"));
        QCOMPARE(h.plugins.plugin(packageId).activeLeases, 0);
        const int before = changed.size();
        QTest::qWait(100);
        QCOMPARE(changed.size(), before);
        QCOMPARE(h.plugins.unload(packageId), PluginOperationResult::Success);
        QVERIFY(h.plugins.load(packageId));
        QVERIFY(!h.registry.sessionFor("local/disabled"));
        const auto instances = h.registry.enabledInstances();
        QCOMPARE(instances.size(), 2);
        for (const auto &instance : instances) QVERIFY(!instance.enabled);
        QVERIFY(h.registry.enableInstance("local/active"));
        QPointer<IMusicSourceSessionV2> reopened = h.registry.sessionFor("local/active");
        QVERIFY(reopened);
        QTRY_COMPARE(reopened->state(), SourceSessionStateV2::Ready);
        QVERIFY(h.registry.closeInstance("local/active"));
        QVERIFY(!reopened);
        QCOMPARE(h.plugins.plugin(packageId).activeLeases, 0);
        QVERIFY(h.accounts.remove("local", "active"));
        QVERIFY(h.plugins.reload(packageId) == PluginOperationResult::Success);
        QVERIFY(!h.registry.sessionFor("local/active"));
        QCOMPARE(h.registry.enabledInstances().size(), 1);
        QVERIFY(!h.registry.enabledInstances().first().enabled);
    }
};

QTEST_MAIN(LocalSourceIntegrationTest)
#include "tst_LocalSourceIntegration.moc"
