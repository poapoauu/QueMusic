#include "core/music/QueueHistoryStore.h"
#include "core/music/QueueHistoryCodec.h"
#include "core/music/PlaybackCoordinator.h"
#include "core/music/PlaybackSink.h"
#include "core/media/SourceAccountStore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

class StoreSink final : public PlaybackSink {
public:
    int plays = 0;
    bool prepare(StreamDescriptorV2, QUuid) override { return true; }
    void play(QUuid) override { ++plays; }
    void stop(QUuid) override {}
};
class StoreSecrets final : public ISecretStore {
public:
    bool write(const QString &, const QByteArray &, QString *) override { return true; }
    std::optional<QByteArray> read(const QString &, QString *) const override { return QByteArray("secret"); }
    bool remove(const QString &, QString *) override { return true; }
};
struct Harness {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    StoreSecrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    StoreSink sink;
    PlaybackCoordinator coordinator{&registry, &sink};
    QString path() const { return dir.filePath("queue-history.json"); }
    bool init() {
        plugins.addSearchPath(QUEMUSIC_TASK12C_PACKAGES);
        return plugins.discover() == 1 && plugins.load("org.quemusic.source.task12c")
            && accounts.saveResolvedV2({"task12c", "home", "Home", {}, {}});
    }
};
static QueueOccurrence occurrence(QString source, QString instance, QString entity) {
    QueueOccurrence item;
    item.occurrenceId = QUuid::createUuid();
    item.ref = {source, instance, {}, MediaEntityTypeV2::Track, entity};
    item.title = "Track"; item.artists = {"Artist"}; item.album = "Album";
    item.durationMs = 10000; item.playableAtEnqueue = true;
    return item;
}
static QByteArray readBytes(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
static bool writeBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

class QueueHistoryStoreTest : public QObject {
    Q_OBJECT
private slots:
    void safeHistoryKeysReplayOnlyMatchingTrustedOccurrences() {
        Harness h; QVERIFY(h.init());
        auto *session = h.registry.sessionFor("task12c/home"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        auto before = occurrence("local", "local/one", "before");
        auto home = occurrence("task12c", "task12c/home", "42"); home.ref.accountId = "home";
        auto other = home; other.occurrenceId = QUuid::createUuid();
        other.ref.sourceInstanceId = "task12c/other"; other.ref.accountId = "other";
        auto gone = home; gone.occurrenceId = QUuid::createUuid();
        QueueHistorySnapshot snapshot; snapshot.queue = {before, home, other};
        for (const auto &item : {home, other, gone}) {
            RecentPlay play{item.occurrenceId, item.ref, "<b>Track</b>", {"Artist"}, "Album",
                            10000, QDateTime::currentDateTimeUtc()};
            snapshot.history.append(play);
        }
        const auto bytes = QueueHistoryCodec::encode(snapshot); QVERIFY(bytes);
        QVERIFY(writeBytes(h.path(), *bytes));
        QueueHistoryStore store(&h.coordinator, h.path()); QVERIFY(store.loadAndAttach());
        QTRY_VERIFY(store.entries().first().toMap().value("replayable").toBool());
        const auto rows = store.entries(); QCOMPARE(rows.size(), 3);
        const QString key = rows.first().toMap().value("key").toString(); QVERIFY(!key.isEmpty());
        for (const auto &value : rows) {
            const auto row = value.toMap();
            QCOMPARE(row.size(), 8);
            for (const auto &field : {"ref", "accountId", "sourceInstanceId", "occurrenceId", "url", "headers", "path", "availableActions"})
                QVERIFY(!row.contains(field));
            QCOMPARE(row.value("title").toString(), QString("<b>Track</b>"));
        }
        QVERIFY(rows.first().toMap().value("replayable").toBool());
        QVERIFY(!rows.at(1).toMap().value("replayable").toBool());
        QVERIFY(!rows.at(2).toMap().value("replayable").toBool());
        QCOMPARE(store.entriesForSource("task12c/home").size(), 2);
        QCOMPARE(store.entriesForSource("task12c/other").size(), 1);
        QVERIFY(store.entriesForSource("missing").isEmpty());
        QVERIFY(!store.playEntry("42")); QVERIFY(!store.playEntry(home.occurrenceId.toString()));
        QVERIFY(!store.playEntry(rows.at(1).toMap().value("key").toString()));
        QVERIFY(!store.playEntry(rows.at(2).toMap().value("key").toString()));
        QVERIFY(h.coordinator.removeOccurrence(before.occurrenceId)); // key is not a cached queue index
        QVERIFY(store.playEntry(key)); QTRY_COMPARE(h.sink.plays, 1);
        QCOMPARE(h.coordinator.currentOccurrence(), home.occurrenceId);
        QTRY_COMPARE(store.history().size(), 4);
        QVERIFY(store.playEntry(key)); QTRY_COMPARE(h.sink.plays, 2); // key survives new records
        QCOMPARE(h.coordinator.currentOccurrence(), home.occurrenceId);
        QVERIFY(h.coordinator.stop());
        QVERIFY(h.registry.disableInstance("task12c/home"));
        QVERIFY(!store.playEntry(key));
        QVERIFY(h.registry.enableInstance("task12c/home"));
        session = h.registry.sessionFor("task12c/home"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        QTRY_VERIFY(store.entries().first().toMap().value("replayable").toBool());
        QVERIFY(h.accounts.saveResolvedV2({"task12c", "other", "Other", {}, {}}));
        auto *otherSession = h.registry.sessionFor("task12c/other"); QVERIFY(otherSession);
        QTRY_COMPARE(otherSession->state(), SourceSessionStateV2::Ready);
        auto rebound = home; rebound.ref = other.ref;
        QVERIFY(h.coordinator.restoreQueue({rebound}));
        QTRY_VERIFY(!h.coordinator.queue().first().toMap().value("unavailable").toBool());
        QVERIFY(!store.playEntry(key)); QVERIFY(!store.playLatest());
        QCOMPARE(h.sink.plays, 2);
    }
    void historyKeysAreEphemeralAndCoordinatorLossIsNotReplayable() {
        Harness h;
        auto item = occurrence("local", "local/one", "42");
        QueueHistorySnapshot snapshot; snapshot.queue = {item};
        snapshot.history = {RecentPlay{item.occurrenceId, item.ref, "Track", {"Artist"}, "Album",
                                      10000, QDateTime::currentDateTimeUtc()}};
        auto bytes = QueueHistoryCodec::encode(snapshot); QVERIFY(bytes); QVERIFY(writeBytes(h.path(), *bytes));
        auto coordinator = std::make_unique<PlaybackCoordinator>(&h.registry, &h.sink);
        QueueHistoryStore store(coordinator.get(), h.path()); QVERIFY(store.loadAndAttach());
        const QString oldKey = store.entries().first().toMap().value("key").toString();
        StoreSink sink2; PlaybackCoordinator restarted(&h.registry, &sink2);
        QueueHistoryStore restored(&restarted, h.path()); QVERIFY(restored.loadAndAttach());
        QVERIFY(restored.entries().first().toMap().value("key").toString() != oldKey);
        QVERIFY(!restored.playEntry(oldKey));
        QSignalSpy changed(&store, &QueueHistoryStore::historyChanged);
        coordinator.reset(); QVERIFY(!changed.isEmpty());
        QCOMPARE(store.entries().size(), 1);
        QVERIFY(!store.entries().first().toMap().value("replayable").toBool());
        QVERIFY(!store.playEntry(oldKey)); QVERIFY(!store.playLatest());
    }
    void historyLimitRevokesOnlyEvictedKeys() {
        Harness h; QVERIFY(h.init());
        auto *session = h.registry.sessionFor("task12c/home"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        auto item = occurrence("task12c", "task12c/home", "42"); item.ref.accountId = "home";
        QueueHistorySnapshot snapshot; snapshot.queue = {item};
        for (int i = 0; i < 200; ++i)
            snapshot.history.append({item.occurrenceId, item.ref, "Track", {"Artist"}, "Album",
                                     10000, QDateTime::currentDateTimeUtc().addSecs(-i)});
        auto bytes = QueueHistoryCodec::encode(snapshot); QVERIFY(bytes); QVERIFY(writeBytes(h.path(), *bytes));
        QueueHistoryStore store(&h.coordinator, h.path()); QVERIFY(store.loadAndAttach());
        QTRY_VERIFY(store.entries().first().toMap().value("replayable").toBool());
        const auto rows = store.entries();
        const auto retained = rows.first().toMap().value("key").toString();
        const auto evicted = rows.last().toMap().value("key").toString();
        QVERIFY(store.playEntry(retained)); QTRY_COMPARE(h.sink.plays, 1);
        QTRY_VERIFY(store.entries().first().toMap().value("key").toString() != retained);
        QCOMPARE(store.entries().size(), 200);
        QVERIFY(!store.playEntry(evicted)); QVERIFY(store.playEntry(retained));
        QTRY_COMPARE(h.sink.plays, 2);
    }
    void persistsMixedQueueAcrossRestart() {
        Harness h;
        auto local = occurrence("local", "local/one", "opaque-local");
        auto remote = occurrence("navidrome", "navidrome/one", "opaque-remote");
        QueueHistoryStore store(&h.coordinator, h.path());
        QVERIFY(store.loadAndAttach());
        QVERIFY(h.coordinator.restoreQueue({local, remote, local} ) == false); // duplicate occurrence rejected
        auto duplicate = local; duplicate.occurrenceId = QUuid::createUuid();
        QVERIFY(h.coordinator.restoreQueue({local, remote, duplicate}));
        QTRY_VERIFY(!readBytes(h.path()).isEmpty());
        const auto bytes = readBytes(h.path());
        QVERIFY(!bytes.contains("file://")); QVERIFY(!bytes.contains("token"));
        QCOMPARE(QueueHistoryCodec::decode(bytes).snapshot.queue.size(), 3);
        StoreSink sink2;
        PlaybackCoordinator restarted(&h.registry, &sink2);
        QueueHistoryStore restored(&restarted, h.path());
        QVERIFY(restored.loadAndAttach());
        const auto queue = restarted.exportQueue();
        QCOMPARE(queue.size(), 3);
        QCOMPARE(queue.at(0).occurrenceId, local.occurrenceId);
        QCOMPARE(queue.at(1).ref, remote.ref);
        QCOMPARE(queue.at(2).occurrenceId, duplicate.occurrenceId);
        QCOMPARE(restarted.currentIndex(), -1);
        QCOMPARE(sink2.plays, 0);
    }
    void successfulStartOnlyAddsHistoryOnce() {
        Harness h; QVERIFY(h.init());
        QueueHistoryStore store(&h.coordinator, h.path()); QVERIFY(store.loadAndAttach());
        auto item = occurrence("task12c", "task12c/home", "42"); item.ref.accountId = "home";
        QVERIFY(h.coordinator.restoreQueue({item}));
        QTRY_VERIFY(!readBytes(h.path()).isEmpty());
        QCOMPARE(store.history().size(), 0);
        QSignalSpy started(&h.coordinator, &PlaybackCoordinator::playbackStarted);
        const auto generation = h.coordinator.playQueueEntry(0);
        QVERIFY(!generation.isNull());
        QTRY_COMPARE(started.size(), 1);
        QTRY_COMPARE(store.history().size(), 1);
        QCOMPARE(store.history().first().occurrenceId, item.occurrenceId);
        QCOMPARE(store.history().first().ref, item.ref);
        QCOMPARE(store.history().first().playedAt.timeSpec(), Qt::UTC);
        QVERIFY(QMetaObject::invokeMethod(&h.coordinator, "playbackStarted", Q_ARG(QUuid, generation)));
        QVERIFY(QMetaObject::invokeMethod(&h.coordinator, "playbackStarted", Q_ARG(QUuid, QUuid::createUuid())));
        QCOMPARE(store.history().size(), 1);
        StoreSink sink2; PlaybackCoordinator restarted(&h.registry, &sink2);
        QueueHistoryStore reloaded(&restarted, h.path()); QVERIFY(reloaded.loadAndAttach());
        QCOMPARE(reloaded.history().size(), 1);
        QCOMPARE(store.latest().value(QStringLiteral("title")).toString(), QStringLiteral("Track"));
        QCOMPARE(store.latest().value(QStringLiteral("artist")).toString(), QStringLiteral("Artist"));
        QVERIFY(store.latest().value(QStringLiteral("replayable")).toBool());
        QVERIFY(!store.latest().contains(QStringLiteral("ref")));
        QVERIFY(!store.latest().contains(QStringLiteral("path")));
        QSignalSpy historyChanged(&store, &QueueHistoryStore::historyChanged);
        QVERIFY(store.playLatest());
        QTRY_COMPARE(h.sink.plays, 2);
        QTRY_COMPARE(store.history().size(), 2);
        QVERIFY(!historyChanged.isEmpty());
        QVERIFY(h.coordinator.stop());
        QVERIFY(h.registry.disableInstance(QStringLiteral("task12c/home")));
        QTRY_VERIFY(!store.latest().value(QStringLiteral("replayable")).toBool());
        QVERIFY(!store.playLatest());
        QVERIFY(h.coordinator.removeOccurrence(item.occurrenceId));
        QVERIFY(!store.latest().value(QStringLiteral("replayable")).toBool());
        QVERIFY(!store.playLatest());
        QCOMPARE(h.sink.plays, 2);
    }
    void futureVersionIsReadOnly() {
        Harness h;
        const QByteArray future = R"({"schemaVersion":2,"private":"preserve"})";
        QVERIFY(writeBytes(h.path(), future));
        QueueHistoryStore store(&h.coordinator, h.path());
        QVERIFY(store.loadAndAttach());
        QVERIFY(!store.warningKey().isEmpty());
        QVERIFY(h.coordinator.restoreQueue({occurrence("local", "local/one", "opaque")}));
        QCoreApplication::processEvents();
        QCOMPARE(readBytes(h.path()), future);
    }
    void corruptFileRequiresBackupBeforeWrite() {
        Harness h;
        const QByteArray corrupt("raw file://secret bytes");
        QVERIFY(writeBytes(h.path(), corrupt));
        QueueHistoryStore::Hooks hooks;
        hooks.backup = [](const QString &, const QString &) { return false; };
        QueueHistoryStore blocked(&h.coordinator, h.path(), nullptr, hooks);
        QVERIFY(!blocked.loadAndAttach());
        QVERIFY(!blocked.warningKey().isEmpty());
        QVERIFY(h.coordinator.restoreQueue({occurrence("local", "local/one", "opaque")}));
        QCoreApplication::processEvents();
        QCOMPARE(readBytes(h.path()), corrupt);
        StoreSink sink2; PlaybackCoordinator coordinator2(&h.registry, &sink2);
        QueueHistoryStore allowed(&coordinator2, h.path());
        QVERIFY(allowed.loadAndAttach());
        QVERIFY(!allowed.backupPath().isEmpty());
        QCOMPARE(readBytes(allowed.backupPath()), corrupt);
        const auto permissions = QFileInfo(allowed.backupPath()).permissions();
        QVERIFY(permissions & QFileDevice::ReadOwner);
        QVERIFY(!(permissions & QFileDevice::ReadGroup));
        QVERIFY(!(permissions & QFileDevice::ReadOther));
        QVERIFY(coordinator2.restoreQueue({occurrence("local", "local/one", "new")}));
        QTRY_COMPARE(QueueHistoryCodec::decode(readBytes(h.path())).status, QueueHistoryDecodeStatus::Ok);
        QCOMPARE(readBytes(allowed.backupPath()), corrupt);
    }
    void failedWriteRetainsPreviousSnapshot() {
        Harness h;
        QueueHistorySnapshot initial;
        initial.queue = {occurrence("local", "local/one", "old")};
        const auto bytes = QueueHistoryCodec::encode(initial); QVERIFY(bytes.has_value());
        QVERIFY(writeBytes(h.path(), *bytes));
        QueueHistoryStore::Hooks hooks;
        bool permitWrite = false;
        hooks.write = [&permitWrite](const QString &path, const QByteArray &data) {
            return permitWrite && writeBytes(path, data);
        };
        QueueHistoryStore store(&h.coordinator, h.path(), nullptr, hooks);
        QVERIFY(store.loadAndAttach());
        QVERIFY(h.coordinator.removeOccurrence(initial.queue.first().occurrenceId));
        QCoreApplication::processEvents();
        QCOMPARE(readBytes(h.path()), *bytes);
        QVERIFY(!store.warningKey().isEmpty());
        permitWrite = true;
        QVERIFY(store.retrySave());
        QCOMPARE(QueueHistoryCodec::decode(readBytes(h.path())).snapshot.queue.size(), 0);
        QVERIFY(store.warningKey().isEmpty());
    }
    void legacyImportPreservesUnmappedBytesAndWarns() {
        Harness h;
        QueueHistoryStore store(&h.coordinator, h.path()); QVERIFY(store.loadAndAttach());
        const auto accepted = occurrence("local", "local/one", "opaque");
        QueueHistorySnapshot snapshot; snapshot.queue = {accepted};
        const auto encoded = QueueHistoryCodec::encode(snapshot); QVERIFY(encoded.has_value());
        const auto item = QJsonDocument::fromJson(*encoded).object().value("queue").toArray().first();
        const QByteArray legacy = QJsonDocument(QJsonObject{{"queue", QJsonArray{item, QJsonObject{{"path", "/private/song"}}}}}).toJson();
        const auto legacyPath = h.dir.filePath("explicit-legacy.json");
        QVERIFY(writeBytes(legacyPath, legacy));
        const auto result = store.importLegacyFile(legacyPath);
        QVERIFY(result.parsed); QCOMPARE(result.accepted.size(), 1); QCOMPARE(result.rejected, 1);
        QCOMPARE(h.coordinator.exportQueue().size(), 1);
        QVERIFY(!store.warningKey().isEmpty());
        QVERIFY(!store.backupPath().isEmpty());
        QCOMPARE(readBytes(store.backupPath()), legacy);
        QCOMPARE(readBytes(legacyPath), legacy);
        QCOMPARE(QueueHistoryCodec::decode(readBytes(h.path())).snapshot.legacyImportVersion, 1);
        const auto again = store.importLegacyFile(legacyPath);
        QVERIFY(!again.parsed);
        QCOMPARE(h.coordinator.exportQueue().size(), 1);
    }
};
QTEST_GUILESS_MAIN(QueueHistoryStoreTest)
#include "tst_QueueHistoryStore.moc"
