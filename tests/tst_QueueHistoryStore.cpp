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
