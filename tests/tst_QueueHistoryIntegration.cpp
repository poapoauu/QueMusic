#include "core/music/PageCache.h"
#include "core/music/PlaybackCoordinator.h"
#include "core/music/PlaybackSink.h"
#include "core/music/QueueHistoryCodec.h"
#include "core/music/QueueHistoryStore.h"
#include "core/media/SourceAccountStore.h"
#include "core/source/SourceRegistry.h"
#include "core/plugins/PluginManager.h"
#include "v2/ISourceProvidersV2.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <chrono>

namespace {
class Secrets final : public ISecretStore {
public:
    bool write(const QString &key, const QByteArray &value, QString *) override
    { values.insert(key, value); return true; }
    std::optional<QByteArray> read(const QString &key, QString *) const override
    { if (!values.contains(key)) return std::nullopt; return values.value(key); }
    bool remove(const QString &key, QString *) override
    { values.remove(key); return true; }
    QHash<QString, QByteArray> values;
};

class Sink final : public PlaybackSink {
public:
    int plays = 0;
    bool prepare(StreamDescriptorV2, QUuid) override { return true; }
    void play(QUuid) override { ++plays; }
    void stop(QUuid) override {}
};

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

struct Harness {
    explicit Harness(PluginManager *pluginManager)
        : plugins(pluginManager), registry(pluginManager, &accounts) {}
    QTemporaryDir storage;
    QSettings settings{storage.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager *plugins;
    SourceRegistry registry;
    Sink sink;
    PlaybackCoordinator coordinator{&registry, &sink};
    QString localAccount = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString localInstance() const { return QStringLiteral("local/") + localAccount; }
    QString storePath() const { return storage.filePath("queue-history.json"); }
    bool saveLocal(const QString &root)
    {
        auto *provider = qobject_cast<IPluginSettingsProviderV2 *>(
            plugins->pluginInstance(QStringLiteral("org.quemusic.source.local")));
        if (!provider) return false;
        SourceAccountSaveV2 request;
        request.pluginPackageId = QStringLiteral("org.quemusic.source.local");
        request.sourceId = QStringLiteral("local");
        request.accountId = localAccount;
        request.displayName = QStringLiteral("Local Fixture");
        request.schema = provider->settingsSchema();
        request.draft = {{QStringLiteral("rootDirectory"), root},
                         {QStringLiteral("scanOnOpen"), false}};
        return accounts.saveValidatedV2(request);
    }
    bool saveNavidrome()
    {
        auto *provider = qobject_cast<IPluginSettingsProviderV2 *>(
            plugins->pluginInstance(QStringLiteral("org.quemusic.source.navidrome")));
        if (!provider) return false;
        SourceAccountSaveV2 request;
        request.pluginPackageId = QStringLiteral("org.quemusic.source.navidrome");
        request.sourceId = QStringLiteral("navidrome");
        request.accountId = QStringLiteral("fixture");
        request.displayName = QStringLiteral("Navidrome Fixture");
        request.enabled = false; // Identity fixture; no external network traffic.
        request.schema = provider->settingsSchema();
        request.draft = {{QStringLiteral("serverUrl"), QStringLiteral("http://127.0.0.1:9")},
                         {QStringLiteral("username"), QStringLiteral("fixture")},
                         {QStringLiteral("password"), QStringLiteral("fixture-secret")},
                         {QStringLiteral("quality"), QStringLiteral("original")}};
        return accounts.saveValidatedV2(request);
    }
    MediaItemV2 localTrack()
    {
        auto *session = registry.sessionFor(localInstance());
        if (!session) return {};
        auto *pages = qobject_cast<IPageProviderV2 *>(session);
        if (!pages) return {};
        PageQueryV2 query;
        query.page = MusicPageKindV2::Category;
        query.section = PageSectionKindV2::Tracks;
        query.scope.sourceInstanceId = localInstance();
        query.filters.insert(QStringLiteral("entityType"), int(MediaEntityTypeV2::Directory));
        QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
        pages->fetchPage(query);
        if (ready.isEmpty() && !ready.wait(5000)) return {};
        const auto root = qvariant_cast<PageResultV2>(ready.last().at(1));
        if (root.sections.isEmpty() || root.sections.first().items.isEmpty()) return {};
        query.filters.remove(QStringLiteral("entityType"));
        query.filters.insert(QStringLiteral("directoryId"), root.sections.first().items.first().ref.entityId);
        ready.clear();
        pages->fetchPage(query);
        if (ready.isEmpty() && !ready.wait(5000)) return {};
        const auto tracks = qvariant_cast<PageResultV2>(ready.last().at(1));
        if (tracks.sections.isEmpty() || tracks.sections.first().items.isEmpty()) return {};
        return tracks.sections.first().items.first();
    }
};

QueueOccurrence occurrence(const MediaRefV2 &ref, const QString &title)
{
    QueueOccurrence value;
    value.occurrenceId = QUuid::createUuid();
    value.ref = ref;
    value.title = title;
    value.artists = {QStringLiteral("Fixture Artist")};
    value.playableAtEnqueue = true;
    return value;
}
QByteArray bytesAt(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class QueueHistoryIntegrationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        plugins.addSearchPath(QStringLiteral(QUEMUSIC_PHASE4_PACKAGES));
        plugins.discover();
        QVERIFY(plugins.load(QStringLiteral("org.quemusic.source.local")));
        QVERIFY(plugins.load(QStringLiteral("org.quemusic.source.navidrome")));
        // On this macOS fixture, unloading Navidrome before application
        // teardown leaves a Qt callback pointing into unmapped code. Keep only
        // that DSO mapped for the process; Local still uses normal lifecycle.
        auto navidromeLease = plugins.acquire(QStringLiteral("org.quemusic.source.navidrome"));
        QVERIFY(navidromeLease.isValid());
        navidromeLease.pinLoadedPackage();
    }

    void localAndNavidromeSurviveRestartWithoutPaths()
    {
        Harness h(&plugins);
        QTemporaryDir music;
        QVERIFY(music.isValid());
        const QString trackPath = music.filePath("song.wav");
        QVERIFY(writeAudio(trackPath));
        QVERIFY(h.saveLocal(music.path()));
        QVERIFY(h.saveNavidrome());
        auto *session = h.registry.sessionFor(h.localInstance());
        QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        const auto track = h.localTrack();
        QVERIFY(!track.ref.entityId.isEmpty());
        QVERIFY(!QUuid(track.ref.entityId).isNull());
        const MediaRefV2 remoteRef{QStringLiteral("navidrome"), QStringLiteral("navidrome/fixture"),
                                   QStringLiteral("fixture"), MediaEntityTypeV2::Track,
                                   QStringLiteral("server-track-42")};
        const auto local = occurrence(track.ref, track.title);
        const auto remote = occurrence(remoteRef, QStringLiteral("Remote"));
        auto secondLocal = local;
        secondLocal.occurrenceId = QUuid::createUuid();
        QueueHistoryStore store(&h.coordinator, h.storePath());
        QVERIFY(store.loadAndAttach());
        QVERIFY(h.coordinator.restoreQueue({local, remote, secondLocal}));
        QTRY_VERIFY(!bytesAt(h.storePath()).isEmpty());
        const auto bytes = bytesAt(h.storePath());
        QVERIFY(!bytes.contains("file://"));
        QVERIFY(!bytes.contains("http://"));
        QVERIFY(!bytes.contains("fixture-secret"));
        QVERIFY(!bytes.contains(QFileInfo(trackPath).canonicalFilePath().toUtf8()));
        Sink restartedSink;
        PlaybackCoordinator restarted(&h.registry, &restartedSink);
        QueueHistoryStore restored(&restarted, h.storePath());
        QVERIFY(restored.loadAndAttach());
        const auto queue = restarted.exportQueue();
        QCOMPARE(queue.size(), 3);
        QCOMPARE(queue.at(0).ref, track.ref);
        QCOMPARE(queue.at(1).ref, remoteRef);
        QCOMPARE(queue.at(2).occurrenceId, secondLocal.occurrenceId);
        QCOMPARE(restarted.currentIndex(), -1);
        QCOMPARE(restartedSink.plays, 0);
    }

    void missingSourceThenReturnPreservesIdentity()
    {
        Harness h(&plugins);
        QTemporaryDir music;
        QVERIFY(writeAudio(music.filePath("song.wav")));
        QVERIFY(h.saveLocal(music.path()));
        auto *session = h.registry.sessionFor(h.localInstance());
        QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        const auto track = h.localTrack();
        QVERIFY(!track.ref.entityId.isEmpty());
        QueueHistoryStore store(&h.coordinator, h.storePath());
        QVERIFY(store.loadAndAttach());
        const auto original = occurrence(track.ref, track.title);
        QVERIFY(h.coordinator.restoreQueue({original}));
        QTRY_VERIFY(!bytesAt(h.storePath()).isEmpty());
        QVERIFY(h.registry.disableInstance(h.localInstance()));
        Sink sink;
        PlaybackCoordinator restarted(&h.registry, &sink);
        QueueHistoryStore restored(&restarted, h.storePath());
        QVERIFY(restored.loadAndAttach());
        QCOMPARE(restarted.exportQueue().first().ref, original.ref);
        QCOMPARE(restarted.queue().first().toMap().value("unavailable").toBool(), true);
        QSignalSpy failed(&restarted, &PlaybackCoordinator::playbackFailed);
        QVERIFY(!restarted.playQueueEntry(0).isNull());
        QTRY_VERIFY(!failed.isEmpty());
        QCOMPARE(restored.history().size(), 0);
        QVERIFY(h.registry.enableInstance(h.localInstance()));
        session = h.registry.sessionFor(h.localInstance());
        QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        const auto rescanned = h.localTrack();
        QCOMPARE(rescanned.ref, original.ref);
        QVERIFY(!restarted.playQueueEntry(0).isNull());
        QTRY_VERIFY(sink.plays == 1 || failed.size() >= 2);
        QVERIFY2(sink.plays == 1, qPrintable(failed.last().at(1).toString()));
        QCOMPARE(restored.history().size(), 1);
        QCOMPARE(restored.history().first().ref, original.ref);
    }

    void staleLocalCacheCannotEnterRestoredQueue()
    {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        PageCacheKeyV2 key;
        key.sourceInstanceIds = {QStringLiteral("local/fixture")};
        key.query.scope.sourceInstanceId = QStringLiteral("local/fixture");
        const auto now = QDateTime::currentDateTimeUtc();
        QString path;
        {
            PageCache cache(storage.path());
            MediaItemV2 item;
            item.ref = {QStringLiteral("local"), QStringLiteral("local/fixture"),
                        QStringLiteral("fixture"), MediaEntityTypeV2::Track,
                        QStringLiteral("opaque-id")};
            PageSectionV2 section;
            section.kind = PageSectionKindV2::Tracks;
            section.items = {item};
            QVERIFY(cache.store(key, PageResultV2{{section}, {}, false, true}, now));
            path = cache.filePath(key);
        }
        QJsonObject root = QJsonDocument::fromJson(bytesAt(path)).object();
        auto page = root.value("page").toObject();
        auto sections = page.value("sections").toArray();
        auto section = sections.first().toObject();
        auto items = section.value("items").toArray();
        auto item = items.first().toObject();
        auto ref = item.value("ref").toObject();
        ref.insert("entityId", QStringLiteral("file:///private/legacy-song.flac"));
        item.insert("ref", ref);
        items[0] = item;
        section.insert("items", items);
        sections[0] = section;
        page.insert("sections", sections);
        root.insert("page", page);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();
        PageCache cache(storage.path());
        QVERIFY(!cache.lookup(key, now, std::chrono::minutes(5)));
        QueueHistorySnapshot snapshot;
        snapshot.queue = {occurrence({QStringLiteral("local"), QStringLiteral("local/fixture"),
                                      QStringLiteral("fixture"), MediaEntityTypeV2::Track,
                                      QStringLiteral("file:///private/legacy-song.flac")},
                                     QStringLiteral("Legacy"))};
        QVERIFY(!QueueHistoryCodec::encode(snapshot));
    }
private:
    PluginManager plugins;
};

QTEST_MAIN(QueueHistoryIntegrationTest)
#include "tst_QueueHistoryIntegration.moc"
