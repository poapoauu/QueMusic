#if defined(QUEMUSIC_ORIGINAL_UI_ADAPTER_FIXTURE)
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

class OriginalUiAdapterSession final : public IMusicSourceSessionV2,
                                       public IPageProviderV2,
                                       public IFavoriteProviderV2,
                                       public IDownloadProviderV2,
                                       public IPlaybackProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IFavoriteProviderV2 IDownloadProviderV2 IPlaybackProviderV2)
public:
    OriginalUiAdapterSession(SourceConfigurationV2 configuration, QObject *parent)
        : IMusicSourceSessionV2(parent), m_configuration(std::move(configuration)) {}

    SourceIdentityV2 identity() const override
    {
        return {QStringLiteral("adapter"), m_configuration.sourceInstanceId,
                m_configuration.accountId, m_configuration.displayName};
    }
    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Ready; }
    CapabilitySetV2 capabilities() const override
    {
        CapabilitySetV2 result;
        for (const auto action : {SourceActionV2::Play, SourceActionV2::Favorite,
                                  SourceActionV2::Unfavorite, SourceActionV2::Download}) {
            result.serverActions.insert(action, {AvailabilityV2::Available, {}, {}});
            result.accountActions.insert(action, {AvailabilityV2::Available, {}, {}});
        }
        if (property("denyFavorite").toBool()) result.accountActions[SourceActionV2::Favorite] = {AvailabilityV2::Forbidden};
        if (property("denyUnfavorite").toBool()) result.accountActions[SourceActionV2::Unfavorite] = {AvailabilityV2::Forbidden};
        if (property("denyDownload").toBool()) result.accountActions[SourceActionV2::Download] = {AvailabilityV2::Forbidden};
        if (property("unsupportedDownload").toBool()) result.serverActions[SourceActionV2::Download] = {AvailabilityV2::Unsupported};
        return result;
    }
    QUuid open() override
    {
        const QUuid id = QUuid::createUuid();
        emit requestStarted(id);
        emit actionCompleted(id, {});
        return id;
    }
    void close() override { emit stateChanged(SourceSessionStateV2::Closing); }
    void cancel(const QUuid &id) override { setProperty("cancelled", id); }
    QUuid fetchPage(const PageQueryV2 &query) override
    {
        const QUuid id = QUuid::createUuid();
        auto requests = property("pageRequests").toList();
        requests.append(QVariantMap{{QStringLiteral("page"), int(query.page)},
                                    {QStringLiteral("section"), int(query.section)},
                                    {QStringLiteral("scope"), query.scope.sourceInstanceId},
                                    {QStringLiteral("search"), query.searchText},
                                    {QStringLiteral("cursor"), query.cursor},
                                    {QStringLiteral("filters"), query.filters}});
        setProperty("pageRequests", requests);
        emit requestStarted(id);
        const int pageFailures = property("pageFailures").toInt();
        if (pageFailures > 0) {
            setProperty("pageFailures", pageFailures - 1);
            emit requestFailed(id, {SourceErrorKindV2::Network,
                                    QStringLiteral("source.network"), {}, std::nullopt, true});
            return id;
        }
        PageResultV2 result;
        if (property("continuable").toBool()) {
            PageSectionV2 section;
            section.kind = query.section;
            section.sectionId = QStringLiteral("fixture-search");
            section.hasMore = query.cursor.isEmpty();
            section.nextCursor = section.hasMore ? QStringLiteral("fixture-next") : QString{};
            MediaItemV2 item;
            item.ref = {QStringLiteral("adapter"), m_configuration.sourceInstanceId,
                        m_configuration.accountId, MediaEntityTypeV2::Track,
                        QStringLiteral("fixture-track")};
            item.title = QStringLiteral("Fixture track");
            item.availableActions.insert(SourceActionV2::Play, {AvailabilityV2::Available, {}, {}});
            section.items = {item};
            result.sections = {section};
        }
        result.sourceStates.insert(m_configuration.sourceInstanceId,
                                   {SourcePageLoadStateV2::Empty, std::nullopt});
        emit pageReady(id, result);
        return id;
    }
    QUuid setFavorite(const MediaRefV2 &media, bool favorite) override
    {
        const QUuid id = QUuid::createUuid();
        setProperty("favoriteRef", mediaRefV2ToVariantMap(media));
        setProperty("favoriteValue", favorite);
        auto requests = property("favoriteRequests").toList();
        requests.append(QVariantMap{{"id", id}, {"ref", mediaRefV2ToVariantMap(media)}, {"favorite", favorite}});
        setProperty("favoriteRequests", requests);
        emit requestStarted(id);
        if (property("holdFavorite").toBool()) return id;
        if (property("failFavorite").toBool()) emit requestFailed(id, {SourceErrorKindV2::Network, "failure", "private diagnostic"});
        else emit actionCompleted(id, {favorite ? SourceActionV2::Favorite : SourceActionV2::Unfavorite,
                                  media, {{QStringLiteral("favorite"), favorite}}});
        return id;
    }
    QUuid download(const MediaRefV2 &media, const QUrl &destination) override
    {
        const auto id = QUuid::createUuid();
        auto requests = property("downloadRequests").toList();
        requests.append(QVariantMap{{"id", id}, {"ref", mediaRefV2ToVariantMap(media)}, {"destination", destination}});
        setProperty("downloadRequests", requests);
        emit requestStarted(id);
        if (property("holdDownload").toBool()) return id;
        if (property("failDownload").toBool()) emit requestFailed(id, {SourceErrorKindV2::Network, "network", "private diagnostic"});
        else emit actionCompleted(id, {SourceActionV2::Download, media, {{"destination", destination}}});
        return id;
    }
    QUuid resolveStream(const MediaRefV2 &media) override
    {
        const QUuid id = QUuid::createUuid();
        setProperty("playRef", mediaRefV2ToVariantMap(media));
        StreamDescriptorV2 stream;
        stream.media = media;
        stream.url = QUrl(QStringLiteral("https://fixture.invalid/stream"));
        stream.headers.insert(QStringLiteral("Authorization"), QStringLiteral("private"));
        emit requestStarted(id);
        emit streamReady(id, stream);
        return id;
    }
    QUuid fetchArtwork(const MediaRefV2 &media) override {
        const auto id = QUuid::createUuid();
        auto requests = property("artworkRequests").toList();
        requests.append(QVariantMap{{"id", id}, {"ref", mediaRefV2ToVariantMap(media)}});
        setProperty("artworkRequests", requests);
        emit requestStarted(id);
        if (!property("holdArtwork").toBool()) emit requestFailed(id, {SourceErrorKindV2::Unsupported});
        return id;
    }
    QUuid fetchLyrics(const MediaRefV2 &media) override {
        const auto id = QUuid::createUuid();
        auto requests = property("lyricsRequests").toList();
        requests.append(QVariantMap{{"id", id}, {"ref", mediaRefV2ToVariantMap(media)}});
        setProperty("lyricsRequests", requests);
        emit requestStarted(id);
        if (property("holdLyrics").toBool()) return id;
        if (property("unsupportedLyrics").toBool()) emit requestFailed(id, {SourceErrorKindV2::Unsupported});
        else if (property("failLyrics").toBool()) emit requestFailed(id, {SourceErrorKindV2::Network, "network", "private diagnostics"});
        else emit actionCompleted(id, {SourceActionV2::Lyrics, media, {{"lyrics", property("lyrics").toString()}}});
        return id;
    }

private:
    SourceConfigurationV2 m_configuration;
};

class OriginalUiAdapterPlugin final : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return 2; }
    SourceDescriptorV2 descriptor() const override
    {
        SourceDescriptorV2 result{QStringLiteral("org.quemusic.source.original-ui-adapter"),
                                  QStringLiteral("adapter"), QStringLiteral("Adapter Fixture"),
                                  QStringLiteral("2.0.0"), 2, {}};
        for (const auto action : {SourceActionV2::Play, SourceActionV2::Favorite,
                                  SourceActionV2::Unfavorite, SourceActionV2::Download})
            result.declaredActions.insert(action, {AvailabilityV2::Available, {}, {}});
        return result;
    }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                         QObject *parent) override
    {
        return new OriginalUiAdapterSession(configuration, parent);
    }
};

#else
#include "MusicHub.h"
#include "MusicPageModel.h"
#include "OnlineListModel.h"
#include "OriginalUiMusicAdapter.h"
#include "PageCache.h"
#include "PlaybackCoordinator.h"
#include "PlaybackSink.h"
#include "SourceAccountStore.h"
#include "SourceScopeStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

namespace {

MediaItemV2 makeItem(MediaEntityTypeV2 type, const QString &source, const QString &instance,
                     const QString &entityId, const QString &title = QStringLiteral("Track 42"))
{
    MediaItemV2 item;
    item.ref = {source, instance, instance + QStringLiteral("-account"), type, entityId};
    item.title = title;
    item.artists = {QStringLiteral("Artist")};
    item.album = QStringLiteral("Album");
    item.subtitle = QStringLiteral("Display subtitle");
    item.artworkId = QStringLiteral("cover-id");
    item.durationMs = 123000;
    item.metadata = {{QStringLiteral("url"), QStringLiteral("https://private.example/stream")},
                     {QStringLiteral("headers"), QVariantMap{{QStringLiteral("Authorization"), QStringLiteral("secret")}}},
                     {QStringLiteral("rawBody"), QStringLiteral("do-not-expose")}};
    return item;
}

MediaItemV2 routedItem(MediaEntityTypeV2 type, const QString &entityId)
{
    MediaItemV2 item = makeItem(type, QStringLiteral("adapter"),
                                QStringLiteral("adapter/home"), entityId);
    item.ref.accountId = QStringLiteral("home");
    for (const auto action : {SourceActionV2::Play, SourceActionV2::Favorite,
                              SourceActionV2::Unfavorite, SourceActionV2::Download})
        item.availableActions.insert(action, {AvailabilityV2::Available, {}, {}});
    return item;
}

PageResultV2 resultWith(const QList<MediaItemV2> &items, const QString &sectionId,
                        const QHash<QString, SourcePageStateV2> &states = {})
{
    PageResultV2 result;
    result.sections = {{sectionId, QStringLiteral("section.title"), PageSectionKindV2::Tracks,
                        QStringLiteral("list"), items, {}, false}};
    result.sourceStates = states;
    return result;
}

void accept(MusicPageModel *model, const PageResultV2 &result)
{
    const quint64 generation = model->beginRequest();
    QVERIFY(model->applyResult(generation, result));
    QVERIFY(model->finishGeneration(generation, 1));
    QCoreApplication::processEvents();
}

class AdapterSecrets final : public ISecretStore {
public:
    bool write(const QString &, const QByteArray &, QString *) override { return true; }
    std::optional<QByteArray> read(const QString &, QString *) const override { return QByteArray{}; }
    bool remove(const QString &, QString *) override { return true; }
};

class AdapterSink final : public PlaybackSink {
public:
    bool prepare(StreamDescriptorV2 value, QUuid valueGeneration) override
    {
        stream = std::move(value);
        generation = valueGeneration;
        ++prepares;
        return true;
    }
    void play(QUuid valueGeneration) override { generation = valueGeneration; ++plays; }
    void stop(QUuid) override { ++stops; }

    StreamDescriptorV2 stream;
    QUuid generation;
    int prepares = 0;
    int plays = 0;
    int stops = 0;
};

struct RoutingHarness {
    QTemporaryDir dir;
    QSettings settings{dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
    AdapterSecrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    SourceScopeStore scope{&settings};
    AdapterSink sink;
    std::unique_ptr<MusicHub> hub;
    std::unique_ptr<PlaybackCoordinator> playback;
    std::unique_ptr<OriginalUiMusicAdapter> adapter;

    bool init(bool addSecondAccount = false)
    {
        plugins.addSearchPath(QStringLiteral(QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGES));
        const int discovered = plugins.discover();
        if (discovered != 1) {
            qWarning() << "adapter fixture discover" << discovered << plugins.plugins();
            return false;
        }
        if (!plugins.load(QStringLiteral("org.quemusic.source.original-ui-adapter"))) {
            qWarning() << "adapter fixture load" << plugins.plugins();
            return false;
        }
        if (!accounts.saveResolvedV2({QStringLiteral("adapter"), QStringLiteral("home"),
                                      QStringLiteral("Home"), {}, {}})) {
            qWarning() << "adapter fixture account";
            return false;
        }
        if (addSecondAccount
            && !accounts.saveResolvedV2({QStringLiteral("adapter"), QStringLiteral("office"),
                                         QStringLiteral("Office"), {}, {}})) {
            qWarning() << "adapter fixture second account";
            return false;
        }
        settings.setValue("MusicHub/cacheDirectory", dir.filePath("cache"));
        hub = std::make_unique<MusicHub>(&registry, &scope, &settings);
        playback = std::make_unique<PlaybackCoordinator>(&registry, &sink);
        adapter = std::make_unique<OriginalUiMusicAdapter>(hub.get(), playback.get());
        return true;
    }
    ~RoutingHarness()
    {
        adapter.reset();
        playback.reset();
        hub.reset();
        musicCacheIoPool()->waitForDone();
    }
    IMusicSourceSessionV2 *session(const QString &id = QStringLiteral("adapter/home"))
    { return registry.sessionFor(id); }
};

} // namespace

class OriginalUiMusicAdapterTest final : public QObject {
    Q_OBJECT
private slots:
    void currentDownloadRequiresIdentityPermissionsAndANewLocalDestination()
    {
        RoutingHarness h; QVERIFY(h.init()); auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdDownload", true);
        const auto track = routedItem(MediaEntityTypeV2::Track, "download-track");
        accept(h.hub->category(), resultWith({track}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        const auto token = h.adapter->currentDownload().value("token").toString(); QVERIFY(!token.isEmpty());
        QVERIFY(h.adapter->currentDownload().value("canDownload").toBool());
        QCOMPARE(h.adapter->currentDownload().keys(), QStringList({"canDownload", "completed", "failed", "pending", "token"}));
        const auto target = QUrl::fromLocalFile(h.dir.filePath("download.bin"));
        QVERIFY(h.adapter->downloadCurrent(target, {}).isNull());
        QVERIFY(h.adapter->downloadCurrent(target, "stale-token").isNull());
        const auto existingPath = h.dir.filePath("existing.bin"); QFile existing(existingPath);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::NewOnly)); QCOMPARE(existing.write("keep"), 4LL); existing.close();
        const QList<QUrl> invalid{QUrl{}, QUrl("https://private.invalid/audio"), QUrl("file://remote/share/a"),
            QUrl("file:relative.bin"), QUrl::fromLocalFile(existingPath), QUrl::fromLocalFile(h.dir.path()),
            QUrl::fromLocalFile(h.dir.filePath("missing/child.bin")), QUrl(target.toString() + "?query=private"),
            QUrl(target.toString() + "#fragment"), QUrl("file://user:private@localhost/a"),
            QUrl::fromLocalFile(h.dir.filePath(QString("a") + QChar::Null + "b"))};
        for (const auto &url : invalid) {
            QVERIFY(h.adapter->downloadCurrent(url, token).isNull());
            QVERIFY(h.adapter->currentDownload().value("failed").toBool());
        }
        const auto link = h.dir.filePath("dangling.bin");
        QVERIFY(QFile::link(h.dir.filePath("not-present.bin"), link));
        QVERIFY(h.adapter->downloadCurrent(QUrl::fromLocalFile(link), token).isNull());
        QVERIFY(session->property("downloadRequests").toList().isEmpty());
        accept(h.hub->category(), resultWith({}, "tracks")); // Route the captured queue item, not current page rows.
        QUuid duplicate;
        const auto observer = connect(h.adapter.get(), &OriginalUiMusicAdapter::currentDownloadChanged, h.adapter.get(), [&] {
            if (h.adapter->currentDownload().value("pending").toBool()) duplicate = h.adapter->downloadCurrent(target, token);
        });
        const auto request = h.adapter->downloadCurrent(target, token); QVERIFY(!request.isNull()); QVERIFY(duplicate.isNull());
        disconnect(observer);
        QCOMPARE(session->property("downloadRequests").toList().size(), 1);
        const auto provider = session->property("downloadRequests").toList().last().toMap();
        QCOMPARE(provider.value("ref").toMap(), mediaRefV2ToVariantMap(track.ref)); QCOMPARE(provider.value("destination").toUrl(), target);
        QVERIFY(h.adapter->currentDownload().value("pending").toBool());
        QVERIFY(!h.adapter->currentDownload().value("completed").toBool());
        emit session->actionCompleted(provider.value("id").toUuid(), {SourceActionV2::Download, track.ref, {{"destination", target}}});
        QTRY_VERIFY(h.adapter->currentDownload().value("completed").toBool());
        QVERIFY(!h.adapter->currentDownload().value("pending").toBool());
        session->setProperty("holdDownload", false); session->setProperty("failDownload", true);
        QVERIFY(!h.adapter->downloadCurrent(target, token).isNull());
        QTRY_VERIFY(h.adapter->currentDownload().value("failed").toBool());
        QVERIFY(!h.adapter->currentDownload().value("completed").toBool());
        session->setProperty("failDownload", false); QVERIFY(!h.adapter->downloadCurrent(target, token).isNull());
        QTRY_VERIFY(h.adapter->currentDownload().value("completed").toBool());
        session->setProperty("holdDownload", true); QVERIFY(!h.adapter->downloadCurrent(target, token).isNull());
        const auto invalidId = session->property("downloadRequests").toList().last().toMap().value("id").toUuid();
        emit session->actionCompleted(invalidId, {SourceActionV2::Download, track.ref,
            {{"destination", QUrl("https://private.invalid/redirect")}, {"diagnostic", "private"}}});
        QTRY_VERIFY(h.adapter->currentDownload().value("failed").toBool());
        QVERIFY(!h.adapter->currentDownload().value("completed").toBool());
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), QByteArray("keep"));
        QVERIFY(h.playback->stop()); QVERIFY(!h.adapter->currentDownload().value("canDownload").toBool());
        QVERIFY(h.adapter->downloadCurrent(target, token).isNull());
    }
    void lateDownloadsCannotUpdateAnotherInstanceOrPlaybackGeneration()
    {
        RoutingHarness h; QVERIFY(h.init(true)); auto *home = h.session(); auto *office = h.session("adapter/office");
        QVERIFY(home); QVERIFY(office); home->setProperty("holdDownload", true); office->setProperty("holdDownload", true);
        const auto a = routedItem(MediaEntityTypeV2::Track, "same-id");
        auto b = a; b.ref.sourceInstanceId = "adapter/office"; b.ref.accountId = "office";
        accept(h.hub->category(), resultWith({a, b}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        const auto tokenA = h.adapter->currentDownload().value("token").toString();
        const auto targetA = QUrl::fromLocalFile(h.dir.filePath("A.bin"));
        QVERIFY(!h.adapter->downloadCurrent(targetA, tokenA).isNull());
        const auto idA = home->property("downloadRequests").toList().last().toMap().value("id").toUuid();
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(1)).isNull()); QTRY_COMPARE(h.sink.plays, 2);
        const auto tokenB = h.adapter->currentDownload().value("token").toString(); QVERIFY(tokenB != tokenA);
        const auto targetB = QUrl::fromLocalFile(h.dir.filePath("B.bin"));
        QVERIFY(h.adapter->downloadCurrent(targetB, tokenA).isNull());
        QVERIFY(!h.adapter->downloadCurrent(targetB, tokenB).isNull());
        const auto idB = office->property("downloadRequests").toList().last().toMap().value("id").toUuid();
        emit home->actionCompleted(idA, {SourceActionV2::Download, a.ref, {{"destination", targetA}}});
        QTest::qWait(20); QVERIFY(h.adapter->currentDownload().value("pending").toBool());
        QVERIFY(!h.adapter->currentDownload().value("completed").toBool());
        emit office->actionCompleted(idB, {SourceActionV2::Download, b.ref, {{"destination", targetB}}});
        QTRY_VERIFY(h.adapter->currentDownload().value("completed").toBool());
        QVERIFY(!h.playback->playQueueEntry(1).isNull());
        QVERIFY(h.adapter->downloadCurrent(targetB, tokenB).isNull());
        QTRY_VERIFY(!h.adapter->currentDownload().value("completed").toBool());
        h.hub.reset(); QVERIFY(!h.adapter->currentDownload().value("canDownload").toBool());
        QVERIFY(h.adapter->downloadCurrent(targetB, h.playback->currentGeneration().toString(QUuid::WithoutBraces)).isNull());
    }
    void downloadPermissionDowngradeDoesNotStopPlayback()
    {
        RoutingHarness h; QVERIFY(h.init()); auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdDownload", true);
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "download-rights")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        const auto generation = h.playback->currentGeneration();
        const auto token = h.adapter->currentDownload().value("token").toString();
        const auto target = QUrl::fromLocalFile(h.dir.filePath("download.bin"));
        QVERIFY(!h.adapter->downloadCurrent(target, token).isNull());
        session->setProperty("denyDownload", true); emit session->capabilitiesChanged(session->capabilities());
        QVERIFY(!h.adapter->currentDownload().value("canDownload").toBool());
        QVERIFY(h.adapter->downloadCurrent(target, token).isNull());
        QTRY_VERIFY(h.adapter->currentDownload().value("failed").toBool());
        QCOMPARE(h.playback->currentGeneration(), generation);
        session->setProperty("denyDownload", false); emit session->capabilitiesChanged(session->capabilities());
        QTRY_VERIFY(h.adapter->currentDownload().value("canDownload").toBool());
        session->setProperty("unsupportedDownload", true); emit session->capabilitiesChanged(session->capabilities());
        QTRY_VERIFY(!h.adapter->currentDownload().value("canDownload").toBool());
        QCOMPARE(h.playback->currentGeneration(), generation);
        QVERIFY(h.registry.disableInstance("adapter/home"));
        QTRY_VERIFY(h.adapter->currentDownload().value("token").toString().isEmpty());
    }
    void downloadSnapshotDoesNotInventMediaRightsOrDropConstraints()
    {
        for (bool constrained : {false, true}) {
            RoutingHarness h; QVERIFY(h.init());
            auto track = routedItem(MediaEntityTypeV2::Track, "download-media-rights");
            if (constrained) track.availableActions[SourceActionV2::Download].constraints.insert("maxBitrate", 128000);
            else track.availableActions.remove(SourceActionV2::Download);
            accept(h.hub->category(), resultWith({track}, "tracks"));
            QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
            QVERIFY(!h.adapter->currentDownload().value("canDownload").toBool());
            QVERIFY(h.adapter->downloadCurrent(QUrl::fromLocalFile(h.dir.filePath("media.bin")),
                                              h.adapter->currentDownload().value("token").toString()).isNull());
            QVERIFY(h.session()->property("downloadRequests").toList().isEmpty());
        }
    }
    void downloadObserversCannotStopOrCreateATargetBeforeDispatch()
    {
        for (bool stop : {true, false}) {
            RoutingHarness h; QVERIFY(h.init());
            accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "download-observer")}, "tracks"));
            QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
            const auto target = QUrl::fromLocalFile(h.dir.filePath("observer.bin"));
            connect(h.adapter.get(), &OriginalUiMusicAdapter::currentDownloadChanged, h.adapter.get(), [&] {
                if (!h.adapter->currentDownload().value("pending").toBool()) return;
                if (stop) h.playback->stop();
                else { QFile file(target.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly)); }
            });
            QVERIFY(h.adapter->downloadCurrent(target, h.adapter->currentDownload().value("token").toString()).isNull());
            QVERIFY(h.session()->property("downloadRequests").toList().isEmpty());
            if (!stop) QVERIFY(h.adapter->currentDownload().value("failed").toBool());
        }
    }
    void currentFavoriteUsesPlaybackIdentityAndConfirmedResults()
    {
        RoutingHarness h; QVERIFY(h.init()); auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdFavorite", true);
        const auto track = routedItem(MediaEntityTypeV2::Track, "current-favorite");
        accept(h.hub->category(), resultWith({track}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(h.sink.plays, 1);
        QVERIFY(h.adapter->currentFavorite().value("canFavorite").toBool());
        QVERIFY(h.adapter->currentFavorite().value("canUnfavorite").toBool());
        QCOMPARE(h.adapter->currentFavorite().value("state").toString(), QString("unknown"));
        QCOMPARE(h.adapter->currentFavorite().keys(), QStringList({"canFavorite", "canUnfavorite", "failed", "pending", "state", "token"}));
        QCOMPARE(h.playback->metaObject()->indexOfMethod("currentActionItem()"), -1);
        QVERIFY(!h.playback->currentItem().contains("availableActions"));
        // Presentation page replacement must not discard current queue-owned identity.
        accept(h.hub->category(), resultWith({}, "tracks"));
        QUuid reentrant;
        const auto connection = connect(h.adapter.get(), &OriginalUiMusicAdapter::currentFavoriteChanged, h.adapter.get(), [&] {
            if (h.adapter->currentFavorite().value("pending").toBool()) reentrant = h.adapter->setCurrentFavorite(true);
        });
        const auto request = h.adapter->setCurrentFavorite(true); QVERIFY(!request.isNull()); QVERIFY(reentrant.isNull());
        disconnect(connection);
        QCOMPARE(session->property("favoriteRequests").toList().size(), 1);
        QCOMPARE(session->property("favoriteRef").toMap(), mediaRefV2ToVariantMap(track.ref));
        QVERIFY(h.adapter->currentFavorite().value("pending").toBool());
        QCOMPARE(h.adapter->currentFavorite().value("state").toString(), QString("unknown"));
        QVERIFY(h.adapter->setCurrentFavorite(false).isNull());
        const auto providerId = session->property("favoriteRequests").toList().last().toMap().value("id").toUuid();
        emit session->actionCompleted(providerId, {SourceActionV2::Favorite, track.ref, {{"favorite", true}}});
        QTRY_COMPARE(h.adapter->currentFavorite().value("state").toString(), QString("favorite"));
        QVERIFY(!h.adapter->currentFavorite().value("pending").toBool());
        session->setProperty("holdFavorite", false); session->setProperty("failFavorite", true);
        QVERIFY(!h.adapter->setCurrentFavorite(false).isNull());
        QTRY_VERIFY(h.adapter->currentFavorite().value("failed").toBool());
        QCOMPARE(h.adapter->currentFavorite().value("state").toString(), QString("favorite"));
        session->setProperty("failFavorite", false);
        QVERIFY(!h.adapter->setCurrentFavorite(false).isNull());
        QTRY_COMPARE(h.adapter->currentFavorite().value("state").toString(), QString("notFavorite"));
        QVERIFY(!h.adapter->currentFavorite().value("failed").toBool());
        QVERIFY(h.playback->stop());
        QVERIFY(!h.adapter->currentFavorite().value("canFavorite").toBool());
        QVERIFY(h.adapter->setCurrentFavorite(true).isNull());
        QTRY_COMPARE(h.adapter->currentFavorite().value("state").toString(), QString("unknown"));
    }
    void lateCurrentFavoriteCannotUpdateAnotherInstanceOrOccurrence()
    {
        RoutingHarness h; QVERIFY(h.init(true)); auto *home = h.session(); auto *office = h.session("adapter/office");
        QVERIFY(home); QVERIFY(office); home->setProperty("holdFavorite", true);
        const auto a = routedItem(MediaEntityTypeV2::Track, "same-id");
        auto b = a; b.ref.sourceInstanceId = "adapter/office"; b.ref.accountId = "office";
        accept(h.hub->category(), resultWith({a, b}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        QVERIFY(!h.adapter->setCurrentFavorite(true).isNull());
        const auto providerId = home->property("favoriteRequests").toList().last().toMap().value("id").toUuid();
        const auto oldToken = h.adapter->currentFavorite().value("token").toString();
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(1)).isNull()); QTRY_COMPARE(h.sink.plays, 2);
        QVERIFY(h.adapter->setCurrentFavorite(true, oldToken).isNull());
        QVERIFY(office->property("favoriteRequests").toList().isEmpty());
        emit home->actionCompleted(providerId, {SourceActionV2::Favorite, a.ref, {{"favorite", true}}});
        QTest::qWait(20);
        QCOMPARE(h.adapter->currentFavorite().value("state").toString(), QString("unknown"));
        QVERIFY(!h.adapter->currentFavorite().value("pending").toBool());
        QVERIFY(!h.adapter->setCurrentFavorite(true).isNull());
        QTRY_COMPARE(h.adapter->currentFavorite().value("state").toString(), QString("favorite"));
        QCOMPARE(office->property("favoriteRef").toMap(), mediaRefV2ToVariantMap(b.ref));
        const auto generation = h.playback->currentGeneration();
        const auto officeToken = h.adapter->currentFavorite().value("token").toString();
        QVERIFY(!h.playback->playQueueEntry(1).isNull());
        QVERIFY(h.adapter->setCurrentFavorite(false, officeToken).isNull());
        QVERIFY(h.playback->currentGeneration() != generation);
        QTRY_COMPARE(h.adapter->currentFavorite().value("state").toString(), QString("unknown"));
        h.hub.reset(); QVERIFY(!h.adapter->currentFavorite().value("canFavorite").toBool());
        QVERIFY(h.adapter->setCurrentFavorite(true).isNull());
    }
    void currentFavoriteDowngradesImmediatelyWithoutStoppingPlayback()
    {
        RoutingHarness h; QVERIFY(h.init()); auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdFavorite", true);
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "favorite-rights")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        QVERIFY(!h.adapter->setCurrentFavorite(true).isNull());
        const auto generation = h.playback->currentGeneration();
        session->setProperty("denyFavorite", true);
        emit session->capabilitiesChanged(session->capabilities());
        QVERIFY(!h.adapter->currentFavorite().value("canFavorite").toBool());
        QVERIFY(h.adapter->setCurrentFavorite(true).isNull());
        QTRY_VERIFY(h.adapter->currentFavorite().value("failed").toBool());
        QTRY_VERIFY(h.adapter->currentFavorite().value("canUnfavorite").toBool());
        QCOMPARE(h.playback->currentGeneration(), generation);
        QCOMPARE(session->property("favoriteRequests").toList().size(), 1);
        session->setProperty("denyFavorite", false); emit session->capabilitiesChanged(session->capabilities());
        QTRY_VERIFY(h.adapter->currentFavorite().value("canFavorite").toBool());
        QVERIFY(h.registry.disableInstance("adapter/home"));
        QVERIFY(!h.adapter->currentFavorite().value("canFavorite").toBool());
        QTRY_VERIFY(!h.adapter->currentFavorite().value("failed").toBool());
    }
    void currentFavoriteStopsBeforeDispatchWhenPresentationObserverStopsPlayback()
    {
        RoutingHarness h; QVERIFY(h.init());
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "favorite-stop")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        connect(h.adapter.get(), &OriginalUiMusicAdapter::currentFavoriteChanged, h.adapter.get(), [&] {
            if (h.adapter->currentFavorite().value("pending").toBool()) h.playback->stop();
        });
        QVERIFY(h.adapter->setCurrentFavorite(true).isNull());
        QVERIFY(h.session()->property("favoriteRequests").toList().isEmpty());
    }
    void restoredQueueDoesNotInventFavoriteRights()
    {
        RoutingHarness h; QVERIFY(h.init());
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "favorite-restore")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull()); QTRY_COMPARE(h.sink.plays, 1);
        const auto history = h.playback->exportQueue(); QVERIFY(h.playback->stop());
        QVERIFY(h.playback->restoreQueue(history)); QVERIFY(!h.playback->playQueueEntry(0).isNull());
        QTRY_COMPARE(h.sink.plays, 2);
        QVERIFY(!h.adapter->currentFavorite().value("canFavorite").toBool());
        QVERIFY(!h.adapter->currentFavorite().value("canUnfavorite").toBool());
        QVERIFY(h.adapter->setCurrentFavorite(true).isNull());
        QVERIFY(!h.adapter->currentDownload().value("canDownload").toBool());
        QVERIFY(h.adapter->downloadCurrent(QUrl::fromLocalFile(h.dir.filePath("restore.bin")),
                                          h.adapter->currentDownload().value("token").toString()).isNull());
    }
    void currentArtworkUsesOnlyCurrentCachedLocalResources()
    {
        RoutingHarness h; QVERIFY(h.init());
        auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdArtwork", true);
        session->setProperty("failLyrics", true);
        const auto first = routedItem(MediaEntityTypeV2::Track, "cover-first");
        const auto second = routedItem(MediaEntityTypeV2::Track, "cover-second");
        accept(h.hub->category(), resultWith({first, second}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(session->property("artworkRequests").toList().size(), 1);
        const auto firstId = session->property("artworkRequests").toList()[0].toMap().value("id").toUuid();
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(1)).isNull());
        QTRY_COMPARE(session->property("artworkRequests").toList().size(), 2);
        const auto secondId = session->property("artworkRequests").toList()[1].toMap().value("id").toUuid();
        const QVariantMap png{{"bytes", QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aX1sAAAAASUVORK5CYII=")},
                              {"mimeType", QString("image/png")}};
        emit session->actionCompleted(firstId, {SourceActionV2::Artwork, first.ref, png});
        QTest::qWait(20); QVERIFY(h.adapter->currentCover().isEmpty());
        emit session->actionCompleted(secondId, {SourceActionV2::Artwork, second.ref, png});
        QTRY_VERIFY(!h.adapter->currentCover().isEmpty());
        const auto cover = h.adapter->currentCover();
        QVERIFY(cover.isLocalFile()); QVERIFY(cover.host().isEmpty());
        QVERIFY(cover.toLocalFile().startsWith(h.dir.filePath("cache/artwork-v2/")));
        QVERIFY(h.playback->stop()); QTRY_VERIFY(h.adapter->currentCover().isEmpty());
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(1)).isNull());
        QTRY_COMPARE(h.adapter->currentCover(), cover); // Cache hit, no provider refetch.
        QCOMPARE(session->property("artworkRequests").toList().size(), 2);
        // Lyric retry must not discard or refetch a healthy cover.
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("failed"));
        QTRY_COMPARE(h.adapter->currentCover(), cover);
        session->setProperty("failLyrics", false);
        h.adapter->retryCurrentLyrics(); QTRY_COMPARE(h.adapter->currentLyricsState(), QString("empty"));
        QCOMPARE(h.adapter->currentCover(), cover);
        QCOMPARE(session->property("artworkRequests").toList().size(), 2);
        QVERIFY(h.registry.disableInstance("adapter/home"));
        QTRY_VERIFY(h.adapter->currentCover().isEmpty());
    }
    void currentArtworkRejectsPluginUrlsAndClearsOnHubRemoval()
    {
        RoutingHarness h; QVERIFY(h.init()); auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdArtwork", true);
        const auto item = routedItem(MediaEntityTypeV2::Track, "cover-url");
        accept(h.hub->category(), resultWith({item}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(session->property("artworkRequests").toList().size(), 1);
        const auto id = session->property("artworkRequests").toList()[0].toMap().value("id").toUuid();
        QSignalSpy failed(h.hub.get(), &MusicHub::assetFailed);
        emit session->actionCompleted(id, {SourceActionV2::Artwork, item.ref,
            {{"url", QUrl("https://private.invalid/cover?token=secret")}, {"headers", QVariantMap{{"Authorization", "secret"}}}}});
        QTRY_COMPARE(failed.size(), 1); QVERIFY(h.adapter->currentCover().isEmpty());
        // Even a spoofed Host notification cannot become a remote UI URL.
        emit h.hub->artworkReady(id, mediaRefV2ToVariantMap(item.ref), QUrl("https://private.invalid/cover"));
        QVERIFY(h.adapter->currentCover().isEmpty());
        h.hub.reset(); QVERIFY(h.adapter->currentCover().isEmpty());
    }
    void currentLyricsAreFencedByPlaybackAndRemainPresentationOnly()
    {
        RoutingHarness h; QVERIFY(h.init());
        auto *session = h.session(); QVERIFY(session);
        session->setProperty("holdLyrics", true);
        const auto first = routedItem(MediaEntityTypeV2::Track, "lyrics-first");
        accept(h.hub->category(), resultWith({first}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(session->property("lyricsRequests").toList().size(), 1);
        const auto firstId = session->property("lyricsRequests").toList().last().toMap().value("id").toUuid();
        const auto second = routedItem(MediaEntityTypeV2::Track, "lyrics-second");
        accept(h.hub->category(), resultWith({second}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(session->property("lyricsRequests").toList().size(), 2);
        const auto secondId = session->property("lyricsRequests").toList().last().toMap().value("id").toUuid();
        emit session->actionCompleted(firstId, {SourceActionV2::Lyrics, first.ref, {{"lyrics", QString("[00:01]Stale")}}});
        QTest::qWait(20);
        QVERIFY(h.adapter->currentLyrics().isEmpty()); QCOMPARE(h.adapter->currentLyricsState(), QString("loading"));
        emit session->actionCompleted(secondId, {SourceActionV2::Lyrics, second.ref,
            {{"lyrics", QString("[00:02.50]Second\n[00:01.123][00:03]First")}}});
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("ready"));
        const auto lines = h.adapter->currentLyrics(); QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].toMap().value("time").toLongLong(), 1123);
        QCOMPARE(lines[1].toMap().value("time").toLongLong(), 2500);
        QCOMPARE(lines[2].toMap().value("time").toLongLong(), 3000);
        for (const auto &line : lines) QCOMPARE(line.toMap().keys(), QStringList({"text", "time"}));
        QVERIFY(h.playback->stop());
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("idle"));
        QVERIFY(h.adapter->currentLyrics().isEmpty());
        const auto third = routedItem(MediaEntityTypeV2::Track, "lyrics-third");
        accept(h.hub->category(), resultWith({third}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(session->property("lyricsRequests").toList().size(), 3);
        QVERIFY(h.registry.disableInstance("adapter/home"));
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("idle"));
        QVERIFY(h.adapter->currentLyrics().isEmpty());
    }

    void currentLyricsRetryAndUnsupportedHaveNoLegacyFallback()
    {
        RoutingHarness h; QVERIFY(h.init());
        auto *session = h.session(); QVERIFY(session);
        session->setProperty("failLyrics", true);
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "lyrics-retry")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("failed"));
        session->setProperty("failLyrics", false); session->setProperty("lyrics", "Plain lyrics");
        h.adapter->retryCurrentLyrics(); h.adapter->retryCurrentLyrics();
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("ready"));
        QCOMPARE(session->property("lyricsRequests").toList().size(), 2);
        QCOMPARE(h.adapter->currentLyrics(), QVariantList({QVariantMap{{"time", 0LL}, {"text", "Plain lyrics"}}}));
        session->setProperty("unsupportedLyrics", true);
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "lyrics-unsupported")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("empty"));
        QVERIFY(h.adapter->currentLyrics().isEmpty()); h.adapter->retryCurrentLyrics();
        QCOMPARE(session->property("lyricsRequests").toList().size(), 3);
        session->setProperty("unsupportedLyrics", false); session->setProperty("lyrics", QString(65537, QLatin1Char('x')));
        accept(h.hub->category(), resultWith({routedItem(MediaEntityTypeV2::Track, "lyrics-too-large")}, "tracks"));
        QVERIFY(!h.adapter->play(h.adapter->categoryItems()->get(0)).isNull());
        QTRY_COMPARE(h.adapter->currentLyricsState(), QString("failed"));
        QVERIFY(h.adapter->currentLyrics().isEmpty());
        h.hub.reset();
        QCOMPARE(h.adapter->currentLyricsState(), QString("idle"));
    }
    void directoryEntityOffersGenericBrowseCapability()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        accept(hub.category(), resultWith({makeItem(MediaEntityTypeV2::Directory,
            "other", "other/one", "opaque-directory")}, "directory"));
        QCOMPARE(adapter.categoryItems()->rowCount(), 1);
        const auto capabilities = adapter.capabilities(adapter.categoryItems()->get(0));
        QVERIFY(capabilities.value("canBrowse").toBool());
    }
    void flattensSectionsAndKeepsFullItemPrivate()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);

        accept(hub.recommendation(), resultWith({makeItem(MediaEntityTypeV2::Track,
            QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("42"))},
            QStringLiteral("recent")));

        QCOMPARE(adapter.recommendSongs()->rowCount(), 1);
        const QVariantMap row = adapter.recommendSongs()->get(0);
        QCOMPARE(row.value(QStringLiteral("title")), QStringLiteral("Track 42"));
        QCOMPARE(row.value(QStringLiteral("artist")), QStringLiteral("Artist"));
        QCOMPARE(row.value(QStringLiteral("album")), QStringLiteral("Album"));
        QCOMPARE(row.value(QStringLiteral("duration")).toLongLong(), 123LL);
        QCOMPARE(row.value(QStringLiteral("source")), QStringLiteral("navidrome"));
        QVERIFY(!row.contains(QStringLiteral("ref")));
        QVERIFY(!row.contains(QStringLiteral("url")));
        QVERIFY(!row.contains(QStringLiteral("headers")));
        QVERIFY(!row.contains(QStringLiteral("metadata")));
        QVERIFY(!row.contains(QStringLiteral("availableActions")));
    }

    void categorySongsFilterEntitiesAndReusePresentationIdentity()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        accept(hub.category(), resultWith({
            makeItem(MediaEntityTypeV2::Album, "source", "source/home", "album"),
            makeItem(MediaEntityTypeV2::Track, "source", "source/home", "track"),
            makeItem(MediaEntityTypeV2::Playlist, "source", "source/home", "playlist")}, "tracks"));
        QCOMPARE(adapter.categorySongs()->rowCount(), 1);
        QCOMPARE(adapter.categorySongs()->get(0), adapter.categoryItems()->get(1));
        QCOMPARE(adapter.categorySongs()->sectionId(), QStringLiteral("tracks"));
    }

    void categoryStatusDistinguishesUnsupportedAndNetworkFailure()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        auto *model = hub.category();
        auto generation = model->beginRequest();
        QCOMPARE(adapter.categoryState(), QStringLiteral("loading"));
        PageSectionV2 section; section.kind = PageSectionKindV2::Tracks; section.sectionId = "tracks";
        QVERIFY(model->applyQueryFailure(generation, section, {SourceErrorKindV2::Unsupported}));
        QVERIFY(model->finishGeneration(generation, 1));
        QCOMPARE(adapter.categoryState(), QStringLiteral("empty"));
        QVERIFY(!adapter.categoryHasError());
        QVERIFY(adapter.categorySongs()->error().isEmpty());
        generation = model->beginRequest();
        QVERIFY(model->applyQueryFailure(generation, section,
                                         {SourceErrorKindV2::Network, "source.network", "private diagnostic"}));
        QVERIFY(model->finishGeneration(generation, 1));
        QCOMPARE(adapter.categoryState(), QStringLiteral("failed"));
        QVERIFY(adapter.categoryHasError());
        QCOMPARE(adapter.categorySongs()->sectionId(), QStringLiteral("tracks"));
    }

    void categoryAggregatesIndependentSectionState()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        auto result = resultWith({makeItem(MediaEntityTypeV2::Track, "source", "source/a", "a")}, "exhausted");
        auto next = result.sections.first();
        next.sectionId = "next-a"; next.hasMore = true; next.nextCursor = "private-cursor-a";
        next.items = {makeItem(MediaEntityTypeV2::Track, "source", "source/b", "b")};
        result.sections.append(next);
        next.sectionId = "next-b"; next.nextCursor = "private-cursor-b";
        result.sections.append(next);
        next.sectionId = "albums"; next.kind = PageSectionKindV2::Albums;
        next.items = {makeItem(MediaEntityTypeV2::Album, "source", "source/a", "album")};
        result.sections.append(next);
        next.sectionId = "invalid-cursor"; next.kind = PageSectionKindV2::Tracks;
        next.nextCursor.clear(); next.items.clear();
        result.sections.append(next);
        auto *model = hub.category();
        const auto generation = model->beginRequest();
        QVERIFY(model->applyResult(generation, result));
        PageSectionV2 unsupported; unsupported.sectionId = "unsupported"; unsupported.kind = PageSectionKindV2::Tracks;
        QVERIFY(model->applyQueryFailure(generation, unsupported, {SourceErrorKindV2::Unsupported}));
        QVERIFY(model->finishGeneration(generation, 2));
        QCOMPARE(adapter.categorySongs()->paginationSectionIds(), QStringList({"next-a", "next-b"}));
        QCOMPARE(adapter.categoryItems()->paginationSectionIds(), QStringList({"next-a", "next-b", "albums"}));
        QVERIFY(adapter.categorySongs()->hasMore());
        QVERIFY(adapter.categorySongs()->retrySectionIds().isEmpty());
        QVERIFY(model->beginSectionRequest(generation, "next-a"));
        QVERIFY(adapter.categorySongs()->loadingMore());
        QCOMPARE(adapter.categorySongs()->paginationSectionIds(), QStringList({"next-b"}));
        QVERIFY(model->applySectionFailure(generation, "next-a",
                                          {SourceErrorKindV2::Network, "network", "private diagnostic"}));
        QCOMPARE(adapter.categorySongs()->retrySectionIds(), QStringList({"next-a"}));
        QCOMPARE(adapter.categorySongs()->paginationSectionIds(), QStringList({"next-b"}));
        QCOMPARE(adapter.categorySongs()->error(), QVariantMap({{"next-a", QVariantMap{{"failed", true}}}}));
        QVERIFY(model->beginSectionRequest(generation, "next-a"));
        QVERIFY(adapter.categorySongs()->retrySectionIds().isEmpty());
        auto recovered = resultWith({}, "next-a");
        QVERIFY(model->applySectionResult(generation, "next-a", recovered, false));
        QVERIFY(adapter.categorySongs()->error().isEmpty());
        QVERIFY(model->beginSectionRequest(generation, "next-b"));
        QVERIFY(model->applySectionFailure(generation, "next-b", {SourceErrorKindV2::Unsupported}));
        // Retained cursors must not keep an unsupported section in a fetch loop.
        QVERIFY(adapter.categorySongs()->paginationSectionIds().isEmpty());
        QVERIFY(adapter.categorySongs()->retrySectionIds().isEmpty());
        QVERIFY(adapter.categorySongs()->error().isEmpty());
        QVERIFY(!adapter.categorySongs()->hasMore());
        QVERIFY(model->resetGeneration(generation));
        QVERIFY(adapter.categorySongs()->paginationSectionIds().isEmpty());
        QVERIFY(adapter.categoryItems()->retrySectionIds().isEmpty());
        QVERIFY(!adapter.categorySongs()->hasMore());
    }

    void categoryCardsKeepIdentityAndUseTheirOwnSectionKinds()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        auto artist = makeItem(MediaEntityTypeV2::Artist, "source", "source/a", "artist");
        auto playlist = makeItem(MediaEntityTypeV2::Playlist, "source", "source/a", "playlist");
        auto chart = makeItem(MediaEntityTypeV2::Playlist, "source", "source/a", "chart");
        chart.metadata = {{"collectionKind", "chart"}};
        auto result = resultWith({artist}, "artists-a");
        result.sections[0].kind = PageSectionKindV2::Artists;
        result.sections[0].hasMore = true; result.sections[0].nextCursor = "artist-private-cursor";
        auto playlists = resultWith({playlist, chart}, "playlists-a").sections.first();
        playlists.kind = PageSectionKindV2::Playlists;
        playlists.hasMore = true; playlists.nextCursor = "playlist-private-cursor";
        result.sections.append(playlists);
        result.sections.append(resultWith({makeItem(MediaEntityTypeV2::Track, "source", "source/a", "track")},
                                         "tracks-a").sections.first());
        auto *model = hub.category();
        const auto generation = model->beginRequest();
        QVERIFY(model->applyResult(generation, result));
        QVERIFY(model->finishGeneration(generation, 1));
        QCOMPARE(adapter.categoryArtists()->rowCount(), 1);
        QCOMPARE(adapter.categoryPlaylists()->rowCount(), 1);
        QCOMPARE(adapter.categoryCharts()->rowCount(), 1);
        QCOMPARE(adapter.categoryArtists()->get(0), adapter.categoryItems()->get(0));
        QCOMPARE(adapter.categoryPlaylists()->get(0), adapter.categoryItems()->get(1));
        QCOMPARE(adapter.categoryCharts()->get(0), adapter.categoryItems()->get(2));
        QCOMPARE(adapter.categoryArtists()->paginationSectionIds(), QStringList({"artists-a"}));
        QCOMPARE(adapter.categoryPlaylists()->paginationSectionIds(), QStringList({"playlists-a"}));
        QCOMPARE(adapter.categoryCharts()->paginationSectionIds(), adapter.categoryPlaylists()->paginationSectionIds());
        QVERIFY(model->beginSectionRequest(generation, "playlists-a"));
        QVERIFY(adapter.categoryPlaylists()->loadingMore());
        QVERIFY(adapter.categoryCharts()->loadingMore());
        QVERIFY(adapter.categoryCharts()->paginationSectionIds().isEmpty());
        QCOMPARE(adapter.categoryArtists()->paginationSectionIds(), QStringList({"artists-a"}));
        QVERIFY(model->applySectionFailure(generation, "playlists-a",
                                          {SourceErrorKindV2::Network, "network", "private diagnostic"}));
        QCOMPARE(adapter.categoryPlaylists()->retrySectionIds(), QStringList({"playlists-a"}));
        QCOMPARE(adapter.categoryCharts()->retrySectionIds(), adapter.categoryPlaylists()->retrySectionIds());
        QVERIFY(adapter.categoryArtists()->error().isEmpty());
        QVERIFY(model->resetGeneration(generation));
        for (auto *projection : {adapter.categoryArtists(), adapter.categoryPlaylists(), adapter.categoryCharts()}) {
            QCOMPARE(projection->rowCount(), 0);
            QVERIFY(projection->paginationSectionIds().isEmpty());
            QVERIFY(projection->retrySectionIds().isEmpty());
            QVERIFY(projection->error().isEmpty());
        }
        // A page of ordinary playlists may be followed by chart data; its cursor
        // must remain available even when the chart projection is currently empty.
        playlists.items = {playlist};
        result.sections = {playlists};
        accept(model, result);
        QCOMPARE(adapter.categoryCharts()->rowCount(), 0);
        QCOMPARE(adapter.categoryCharts()->paginationSectionIds(), QStringList({"playlists-a"}));
    }

    void categoryBackRestoresSanitizedParentContext()
    {
        RoutingHarness harness; QVERIFY(harness.init());
        auto artist = routedItem(MediaEntityTypeV2::Artist, QStringLiteral("artist-42"));
        artist.title = "Parent artist";
        auto album = routedItem(MediaEntityTypeV2::Album, QStringLiteral("album-42"));
        album.title = "Child album";
        accept(harness.hub->recommendation(), resultWith({artist, album}, QStringLiteral("recommend")));
        QVERIFY(harness.adapter->browse(harness.adapter->recommendSongs()->get(0)));
        QVERIFY(harness.adapter->browse(harness.adapter->recommendSongs()->get(1)));
        QCOMPARE(harness.adapter->categoryTitle(), album.title);
        QVERIFY(harness.adapter->categoryBack());
        QVERIFY(harness.adapter->categoryCanNavigateBack());
        QCOMPARE(harness.adapter->categoryTitle(), artist.title);
        QCOMPARE(harness.hub->categoryContext().value("item").toMap().value("ref").toMap()
                     .value("entityId").toString(), artist.ref.entityId);
        QVERIFY(harness.adapter->categoryBack());
        QVERIFY(!harness.adapter->categoryCanNavigateBack());
        QVERIFY(harness.adapter->categoryTitle().isEmpty());
        QVERIFY(!harness.adapter->categoryBack());
    }

    void chartClassificationIsExposedOnlyForPlaylists()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        auto chart = makeItem(MediaEntityTypeV2::Playlist, "charts", "charts/home", "chart-42");
        chart.metadata = {{"collectionKind", QStringLiteral("chart")}};
        auto track = makeItem(MediaEntityTypeV2::Track, "charts", "charts/home", "track-42");
        track.metadata = chart.metadata;
        auto unknown = chart; unknown.metadata = {{"collectionKind", QStringLiteral("private-kind")}};
        accept(hub.category(), resultWith({chart, track, unknown}, QStringLiteral("playlists")));
        QCOMPARE(adapter.categoryItems()->get(0).value("collectionKind").toString(), QStringLiteral("chart"));
        QVERIFY(adapter.categoryItems()->get(1).value("collectionKind").toString().isEmpty());
        QVERIFY(adapter.categoryItems()->get(2).value("collectionKind").toString().isEmpty());
        QVERIFY(!adapter.categoryItems()->get(0).contains("metadata"));
        QVERIFY(!adapter.categoryItems()->get(0).contains("ref"));
    }

    void keepsDuplicateTitlesFromDifferentSourcesDistinct()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        accept(hub.searchResults(), resultWith({
            makeItem(MediaEntityTypeV2::Track, QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("42"), QStringLiteral("Same title")),
            makeItem(MediaEntityTypeV2::Track, QStringLiteral("jellyfin"), QStringLiteral("nas-b"), QStringLiteral("99"), QStringLiteral("Same title"))},
            QStringLiteral("search")));

        QCOMPARE(adapter.searchSongs()->rowCount(), 2);
        const auto first = adapter.searchSongs()->get(0);
        const auto second = adapter.searchSongs()->get(1);
        QCOMPARE(first.value(QStringLiteral("title")), second.value(QStringLiteral("title")));
        QVERIFY(first.value(QStringLiteral("_adapterKey")) != second.value(QStringLiteral("_adapterKey")));
        QCOMPARE(first.value(QStringLiteral("source")), QStringLiteral("navidrome"));
        QCOMPARE(second.value(QStringLiteral("source")), QStringLiteral("jellyfin"));
    }

    void preservesSuccessfulAggregateRowsDuringPartialFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        SourceErrorV2 error{SourceErrorKindV2::Network, QStringLiteral("source.network")};
        accept(hub.category(), resultWith({makeItem(MediaEntityTypeV2::Album,
            QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("album-1"))},
            QStringLiteral("albums"), {{QStringLiteral("nas-a"), {SourcePageLoadStateV2::Ready, std::nullopt}},
                                        {QStringLiteral("nas-b"), {SourcePageLoadStateV2::Failed, error}}}));

        QCOMPARE(hub.category()->state(), PageLoadStateV2::Ready);
        QCOMPARE(adapter.categoryItems()->rowCount(), 1);
        QCOMPARE(adapter.categoryItems()->get(0).value(QStringLiteral("title")), QStringLiteral("Track 42"));
    }

    void sharesSelectedSourceScopeWithHub()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);

        adapter.setSelectedSourceInstanceId(QStringLiteral("navidrome/home"));

        QCOMPARE(adapter.selectedSourceInstanceId(), QStringLiteral("navidrome/home"));
        QCOMPARE(hub.selectedSourceInstanceId(), QStringLiteral("navidrome/home"));
    }

    void categoryRefreshForwardsToTheActiveSource()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        harness.adapter->setSelectedSourceInstanceId(QStringLiteral("adapter/home"));
        harness.adapter->activatePage(1);
        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 5);

        harness.adapter->refreshPage(1);
        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 10);
        for (const QVariant &request : harness.session()->property("pageRequests").toList())
            QCOMPARE(request.toMap().value(QStringLiteral("scope")).toString(),
                     QStringLiteral("adapter/home"));
    }

    void splitsFavoriteTracksAndPlaylistsByEntityType()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        accept(hub.favorites(), resultWith({
            makeItem(MediaEntityTypeV2::Track, QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("track")),
            makeItem(MediaEntityTypeV2::Playlist, QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("playlist")),
            makeItem(MediaEntityTypeV2::Album, QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("album"))},
            QStringLiteral("favorites")));

        QCOMPARE(adapter.favoriteSongs()->rowCount(), 1);
        QCOMPARE(adapter.favoriteLists()->rowCount(), 1);
        QCOMPARE(adapter.favoriteSongs()->get(0).value(QStringLiteral("entityType")).toInt(), int(MediaEntityTypeV2::Track));
        QCOMPARE(adapter.favoriteLists()->get(0).value(QStringLiteral("entityType")).toInt(), int(MediaEntityTypeV2::Playlist));
    }

    void replacesRowsWhenAnAcceptedGenerationSupersedesThem()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        accept(hub.recommendation(), resultWith({makeItem(MediaEntityTypeV2::Track,
            QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("old"), QStringLiteral("Old"))},
            QStringLiteral("recommend")));
        const quint64 staleGeneration = hub.recommendation()->beginRequest();
        QVERIFY(hub.recommendation()->applyResult(staleGeneration, resultWith({makeItem(MediaEntityTypeV2::Track,
            QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("new"), QStringLiteral("New"))},
            QStringLiteral("recommend"))));
        QVERIFY(hub.recommendation()->finishGeneration(staleGeneration, 1));
        QCoreApplication::processEvents();

        QCOMPARE(adapter.recommendSongs()->rowCount(), 1);
        QCOMPARE(adapter.recommendSongs()->get(0).value(QStringLiteral("title")), QStringLiteral("New"));
    }

    void rejectsUnknownPresentationRows()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        const QVariantMap unknown{{QStringLiteral("_adapterKey"), QVariant::fromValue<qulonglong>(999)}};

        QVERIFY(!adapter.browse(unknown));
        QVERIFY(adapter.play(unknown).isNull());
        QVERIFY(adapter.enqueue(unknown).isNull());
        QVERIFY(adapter.setFavorite(unknown, true).isNull());
    }

    void projectsCapabilitiesAndRejectsUnavailableActions()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        MediaItemV2 item = routedItem(MediaEntityTypeV2::Track, QStringLiteral("capability-track"));
        item.availableActions = {
            {SourceActionV2::Play, {AvailabilityV2::Available, {}, {}}},
            {SourceActionV2::Favorite, {AvailabilityV2::Unsupported,
                                        QStringLiteral("music.favorite.unsupported"), {}}},
            {SourceActionV2::Unfavorite, {AvailabilityV2::Unavailable,
                                          QStringLiteral("music.favorite.signIn"), {}}}};
        accept(harness.hub->recommendation(), resultWith({item}, QStringLiteral("recommend")));
        const QVariantMap row = harness.adapter->recommendSongs()->get(0);

        QVariantMap projected;
        QVERIFY(QMetaObject::invokeMethod(harness.adapter.get(), "capabilities",
                                          Q_RETURN_ARG(QVariantMap, projected),
                                          Q_ARG(QVariant, QVariant(row))));
        QVERIFY(projected.value(QStringLiteral("canPlay")).toBool());
        QVERIFY(projected.value(QStringLiteral("canEnqueue")).toBool());
        QVERIFY(!projected.value(QStringLiteral("canFavorite")).toBool());
        QCOMPARE(projected.value(QStringLiteral("favoriteReasonKey")).toString(),
                 QStringLiteral("music.favorite.unsupported"));
        QVERIFY(!projected.value(QStringLiteral("canUnfavorite")).toBool());
        QCOMPARE(projected.value(QStringLiteral("unfavoriteReasonKey")).toString(),
                 QStringLiteral("music.favorite.signIn"));
        QVERIFY(harness.adapter->setFavorite(row, true).isNull());
        QVERIFY(harness.adapter->setFavorite(row, false).isNull());
    }

    void intersectsCapabilitiesForMixedPresentationSelections()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        MediaItemV2 first = routedItem(MediaEntityTypeV2::Track, QStringLiteral("first"));
        first.availableActions = {{SourceActionV2::Play, {AvailabilityV2::Available, {}, {}}},
                                  {SourceActionV2::Favorite, {AvailabilityV2::Available, {}, {}}}};
        MediaItemV2 second = routedItem(MediaEntityTypeV2::Track, QStringLiteral("second"));
        second.availableActions = {{SourceActionV2::Play, {AvailabilityV2::Available, {}, {}}},
                                   {SourceActionV2::Favorite, {AvailabilityV2::Unsupported,
                                                               QStringLiteral("music.favorite.unsupported"), {}}}};
        accept(harness.hub->recommendation(), resultWith({first, second}, QStringLiteral("recommend")));

        QVariantMap projected;
        const QVariantList rows{harness.adapter->recommendSongs()->get(0),
                                harness.adapter->recommendSongs()->get(1)};
        QVERIFY(QMetaObject::invokeMethod(harness.adapter.get(), "capabilities",
                                          Q_RETURN_ARG(QVariantMap, projected),
                                          Q_ARG(QVariant, QVariant(rows))));
        QVERIFY(projected.value(QStringLiteral("canPlay")).toBool());
        QVERIFY(!projected.value(QStringLiteral("canFavorite")).toBool());
        QCOMPARE(projected.value(QStringLiteral("favoriteReasonKey")).toString(),
                 QStringLiteral("music.favorite.unsupported"));
    }

    void searchesTheRequestedTabThroughTheUnifiedQuery()
    {
        RoutingHarness harness;
        QVERIFY(harness.init(true));
        harness.adapter->setSelectedSourceInstanceId(QStringLiteral("adapter/home"));

        harness.adapter->search(QStringLiteral("needle"), 2);

        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 1);
        const QVariantMap request = harness.session()->property("pageRequests").toList().first().toMap();
        QCOMPARE(request.value(QStringLiteral("page")).toInt(), int(MusicPageKindV2::Search));
        QCOMPARE(request.value(QStringLiteral("section")).toInt(), int(PageSectionKindV2::Albums));
        QCOMPARE(request.value(QStringLiteral("search")).toString(), QStringLiteral("needle"));
        QCOMPARE(request.value(QStringLiteral("scope")).toString(), QStringLiteral("adapter/home"));
        QVERIFY(harness.session(QStringLiteral("adapter/office"))
                    ->property("pageRequests").toList().isEmpty());
    }

    void exposesSectionIdentityAndStateOnFlattenedRows()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        PageResultV2 result = resultWith({makeItem(MediaEntityTypeV2::Track,
            QStringLiteral("navidrome"), QStringLiteral("nas-a"), QStringLiteral("42"))},
            QStringLiteral("search-tracks"));
        result.sections.first().hasMore = true;
        result.sections.first().nextCursor = QStringLiteral("next");
        accept(hub.searchResults(), result);

        const QVariantMap row = adapter.searchSongs()->get(0);
        QCOMPARE(row.value(QStringLiteral("sectionId")).toString(), QStringLiteral("search-tracks"));
        QVERIFY(row.value(QStringLiteral("hasMore")).toBool());
        QVERIFY(!row.value(QStringLiteral("loadingMore")).toBool());
        QVERIFY(row.value(QStringLiteral("error")).toMap().isEmpty());
    }

    void retainsRetryStateWhenASectionHasNoPresentationRows()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        auto *search = hub.searchResults();
        const quint64 generation = search->beginRequest();
        PageSectionV2 section;
        section.kind = PageSectionKindV2::Tracks;
        section.sectionId = QStringLiteral("failed-search-tracks");
        QVERIFY(search->applyQueryFailure(generation, section,
                                           {SourceErrorKindV2::Network, QStringLiteral("source.network")}));
        QVERIFY(search->finishGeneration(generation, 1));
        QCoreApplication::processEvents();

        QCOMPARE(adapter.searchSongs()->rowCount(), 0);
        QCOMPARE(adapter.searchSongs()->sectionId(), QStringLiteral("failed-search-tracks"));
        QCOMPARE(adapter.searchSongs()->retrySectionIds(), QStringList({"failed-search-tracks"}));
        QCOMPARE(adapter.searchSongs()->error(), QVariantMap({{"failed-search-tracks", QVariantMap{{"failed", true}}}}));
    }

    void recommendationsAggregateStandardSectionsAndRetainEmptyRetries()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        auto result = resultWith({makeItem(MediaEntityTypeV2::Track, "source", "source/a", "track")}, "recent-exhausted");
        result.sections[0].kind = PageSectionKindV2::RecentlyPlayed;
        auto next = result.sections[0]; next.sectionId = "frequent"; next.kind = PageSectionKindV2::FrequentlyPlayed;
        next.hasMore = true; next.nextCursor = "private-frequent";
        result.sections.append(next);
        next.sectionId = "rated"; next.kind = PageSectionKindV2::HighestRated; next.nextCursor = "private-rated";
        result.sections.append(next);
        auto *model = hub.recommendation();
        const auto generation = model->beginRequest();
        QVERIFY(model->applyResult(generation, result));
        PageSectionV2 failed; failed.sectionId = "newest-failed"; failed.kind = PageSectionKindV2::Newest;
        QVERIFY(model->applyQueryFailure(generation, failed, {SourceErrorKindV2::Network, "network", "private diagnostic"}));
        failed.sectionId = "random-unsupported"; failed.kind = PageSectionKindV2::Random;
        QVERIFY(model->applyQueryFailure(generation, failed, {SourceErrorKindV2::Unsupported}));
        QVERIFY(model->finishGeneration(generation, 3));
        auto *view = adapter.recommendSongs();
        QCOMPARE(view->rowCount(), 3);
        QCOMPARE(view->paginationSectionIds(), QStringList({"frequent", "rated"}));
        QCOMPARE(view->retrySectionIds(), QStringList({"newest-failed"}));
        QCOMPARE(view->error(), QVariantMap({{"newest-failed", QVariantMap{{"failed", true}}}}));
        QVERIFY(model->beginSectionRequest(generation, "frequent"));
        QVERIFY(view->loadingMore());
        QCOMPARE(view->paginationSectionIds(), QStringList({"rated"}));
        QVERIFY(model->beginSectionRequest(generation, "newest-failed"));
        QVERIFY(view->retrySectionIds().isEmpty());
        QVERIFY(model->resetGeneration(generation));
        QCOMPARE(view->rowCount(), 0);
        QVERIFY(view->paginationSectionIds().isEmpty());
        QVERIFY(view->retrySectionIds().isEmpty());
        const auto retryGeneration = model->beginRequest();
        failed.sectionId = "newest-empty"; failed.kind = PageSectionKindV2::Newest;
        QVERIFY(model->applyQueryFailure(retryGeneration, failed, {SourceErrorKindV2::Network}));
        QVERIFY(model->finishGeneration(retryGeneration, 1));
        QCOMPARE(view->rowCount(), 0);
        QCOMPARE(view->retrySectionIds(), QStringList({"newest-empty"}));
    }

    void searchAndFavoritesAggregateOnlyTheirMatchingSections()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SourceScopeStore scope(&settings);
        MusicHub hub(nullptr, &scope, &settings);
        OriginalUiMusicAdapter adapter(&hub, nullptr);
        for (const bool search : {true, false}) {
            auto *model = search ? hub.searchResults() : hub.favorites();
            auto *tracks = search ? adapter.searchSongs() : adapter.favoriteSongs();
            auto *lists = search ? adapter.searchLists() : adapter.favoriteLists();
            const auto kind = search ? PageSectionKindV2::Tracks : PageSectionKindV2::FavoriteTracks;
            auto result = resultWith({makeItem(MediaEntityTypeV2::Track, "source", "source/a", "track")}, "track-a");
            result.sections[0].kind = kind;
            result.sections[0].hasMore = true; result.sections[0].nextCursor = "private-a";
            auto next = result.sections[0]; next.sectionId = "track-b"; next.nextCursor = "private-b";
            result.sections.append(next);
            next.sectionId = "track-exhausted"; next.hasMore = false; next.nextCursor.clear();
            result.sections.append(next);
            next = result.sections[0]; next.sectionId = "playlist-a"; next.kind = PageSectionKindV2::Playlists;
            next.items = {makeItem(MediaEntityTypeV2::Playlist, "source", "source/a", "playlist")};
            result.sections.append(next);
            const auto generation = model->beginRequest();
            QVERIFY(model->applyResult(generation, result));
            PageSectionV2 failed; failed.sectionId = "track-failed"; failed.kind = kind;
            QVERIFY(model->applyQueryFailure(generation, failed, {SourceErrorKindV2::Network, "network", "private diagnostic"}));
            failed.sectionId = "track-unsupported";
            QVERIFY(model->applyQueryFailure(generation, failed, {SourceErrorKindV2::Unsupported}));
            QVERIFY(model->finishGeneration(generation, 3));
            QCOMPARE(tracks->rowCount(), 3);
            QCOMPARE(tracks->paginationSectionIds(), QStringList({"track-a", "track-b"}));
            QCOMPARE(tracks->retrySectionIds(), QStringList({"track-failed"}));
            QCOMPARE(tracks->error(), QVariantMap({{"track-failed", QVariantMap{{"failed", true}}}}));
            QCOMPARE(lists->paginationSectionIds(), QStringList({"playlist-a"}));
            QVERIFY(lists->retrySectionIds().isEmpty());
            QVERIFY(lists->error().isEmpty());
            if (search) {
                QCOMPARE(adapter.searchLyrics()->paginationSectionIds(), tracks->paginationSectionIds());
                QCOMPARE(adapter.searchLyrics()->retrySectionIds(), tracks->retrySectionIds());
                QVERIFY(adapter.searchAlbums()->paginationSectionIds().isEmpty());
            }
            QVERIFY(model->beginSectionRequest(generation, "track-a"));
            QVERIFY(tracks->loadingMore());
            QCOMPARE(tracks->paginationSectionIds(), QStringList({"track-b"}));
            QVERIFY(model->beginSectionRequest(generation, "track-failed"));
            QVERIFY(tracks->retrySectionIds().isEmpty());
            QVERIFY(model->resetGeneration(generation));
            QCOMPARE(tracks->rowCount(), 0);
            QVERIFY(tracks->paginationSectionIds().isEmpty());
            QVERIFY(tracks->retrySectionIds().isEmpty());
            QVERIFY(tracks->error().isEmpty());
        }
    }

    void continuesTheFlattenedSearchSectionByItsPresentationId()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        harness.session()->setProperty("continuable", true);

        harness.adapter->search(QStringLiteral("needle"), 0);
        QTRY_COMPARE(harness.adapter->searchSongs()->rowCount(), 1);
        const QVariantMap row = harness.adapter->searchSongs()->get(0);
        QCOMPARE(row.value(QStringLiteral("sectionId")).toString(), QStringLiteral("fixture-search"));
        QVERIFY(row.value(QStringLiteral("hasMore")).toBool());

        harness.adapter->loadMore(3, row.value(QStringLiteral("sectionId")).toString());

        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 2);
        QCOMPARE(harness.session()->property("pageRequests").toList().last().toMap()
                     .value(QStringLiteral("cursor")).toString(), QStringLiteral("fixture-next"));
        QTRY_VERIFY(!harness.adapter->searchSongs()->hasMore());

        harness.adapter->loadMore(3, row.value(QStringLiteral("sectionId")).toString());
        QCoreApplication::processEvents();
        QCOMPARE(harness.session()->property("pageRequests").toList().size(), 2);
    }

    void retriesTheFailedSearchSectionWithItsTabAndSourceScope()
    {
        RoutingHarness harness;
        QVERIFY(harness.init(true));
        harness.adapter->setSelectedSourceInstanceId(QStringLiteral("adapter/home"));
        harness.session()->setProperty("pageFailures", 1);

        harness.adapter->search(QStringLiteral("needle"), 1);

        QTRY_VERIFY(!harness.adapter->searchLists()->error().isEmpty());
        const QString sectionId = harness.adapter->searchLists()->sectionId();
        QVERIFY(!sectionId.isEmpty());
        QCOMPARE(harness.session()->property("pageRequests").toList().size(), 1);

        harness.adapter->retry(3, sectionId);

        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 2);
        const QVariantMap retry = harness.session()->property("pageRequests").toList().last().toMap();
        QCOMPARE(retry.value(QStringLiteral("section")).toInt(), int(PageSectionKindV2::Playlists));
        QCOMPARE(retry.value(QStringLiteral("scope")).toString(), QStringLiteral("adapter/home"));
        QCOMPARE(retry.value(QStringLiteral("search")).toString(), QStringLiteral("needle"));
        QVERIFY(retry.value(QStringLiteral("cursor")).toString().isEmpty());
        QVERIFY(harness.session(QStringLiteral("adapter/office"))
                    ->property("pageRequests").toList().isEmpty());
    }

    void doesNotExposeFullV2ItemsThroughTheMetaObject()
    {
        const QMetaObject &metaObject = OriginalUiMusicAdapter::staticMetaObject;
        QCOMPARE(metaObject.indexOfMethod("fullItem(QVariantMap)"), -1);
        QCOMPARE(metaObject.indexOfMethod("resolvePresentationItem(QVariantMap)"), -1);
    }

    void clearsPresentationStateWhenHubIsDestroyed()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        accept(harness.hub->recommendation(), resultWith({
            routedItem(MediaEntityTypeV2::Track, QStringLiteral("recommend")),
            routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("recommend-list"))}, QStringLiteral("recommend")));
        accept(harness.hub->category(), resultWith({routedItem(
            MediaEntityTypeV2::Album, QStringLiteral("category"))}, QStringLiteral("category")));
        accept(harness.hub->favorites(), resultWith({
            routedItem(MediaEntityTypeV2::Track, QStringLiteral("favorite-track")),
            routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("favorite-list"))}, QStringLiteral("favorites")));
        accept(harness.hub->searchResults(), resultWith({routedItem(
            MediaEntityTypeV2::Track, QStringLiteral("search"))}, QStringLiteral("search")));
        harness.adapter->setSelectedSourceInstanceId(QStringLiteral("adapter/home"));
        const QVariantMap staleTrack = harness.adapter->recommendSongs()->get(0);
        const QVariantMap stalePlaylist = harness.adapter->recommendSongs()->get(1);

        harness.hub.reset();
        QCoreApplication::processEvents();

        QCOMPARE(harness.adapter->recommendSongs()->rowCount(), 0);
        QCOMPARE(harness.adapter->categoryItems()->rowCount(), 0);
        QCOMPARE(harness.adapter->favoriteSongs()->rowCount(), 0);
        QCOMPARE(harness.adapter->favoriteLists()->rowCount(), 0);
        QCOMPARE(harness.adapter->searchSongs()->rowCount(), 0);
        QVERIFY(harness.adapter->sourceOptions().isEmpty());
        QVERIFY(harness.adapter->selectedSourceInstanceId().isEmpty());
        QVERIFY(!harness.adapter->browse(stalePlaylist));
        QVERIFY(harness.adapter->play(staleTrack).isNull());
        QVERIFY(harness.adapter->enqueue(staleTrack).isNull());
        QVERIFY(harness.adapter->setFavorite(staleTrack, true).isNull());
    }

    void browseRoutesThePrivatePlaylistIdentity()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        auto chart = routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("playlist-42"));
        chart.metadata = {{"collectionKind", QStringLiteral("chart")}};
        accept(harness.hub->recommendation(), resultWith({chart}, QStringLiteral("recommend")));
        const QVariantMap row = harness.adapter->recommendSongs()->get(0);
        QCOMPARE(row.value("collectionKind").toString(), QStringLiteral("chart"));

        QVERIFY(harness.adapter->browse(row));

        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 1);
        const QVariantMap request = harness.session()->property("pageRequests").toList().first().toMap();
        QCOMPARE(request.value(QStringLiteral("scope")), QStringLiteral("adapter/home"));
        QCOMPARE(request.value(QStringLiteral("filters")).toMap(),
                 (QVariantMap{{QStringLiteral("playlistId"), QStringLiteral("playlist-42")}}));
    }

    void closingBrowseRestoresCategoryRoot()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        accept(harness.hub->recommendation(), resultWith({
            routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("playlist-42")),
            routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("playlist-43"))},
            QStringLiteral("recommend")));
        QVERIFY(harness.adapter->browse(harness.adapter->recommendSongs()->get(0)));
        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 1);
        QVERIFY(harness.adapter->browse(harness.adapter->recommendSongs()->get(1)));
        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 2);
        QVERIFY(harness.hub->canNavigateBack());
        harness.adapter->closeCategoryBrowse();
        QVERIFY(!harness.hub->canNavigateBack());
        QVERIFY(harness.hub->categoryContext().isEmpty());
        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 7);
        const auto requests = harness.session()->property("pageRequests").toList();
        for (int i = 2; i < requests.size(); ++i)
            QVERIFY(requests.at(i).toMap().value(QStringLiteral("filters")).toMap().isEmpty());
        harness.adapter->closeCategoryBrowse();
        QCOMPARE(harness.session()->property("pageRequests").toList().size(), 7);
    }

    void favoriteRoutesThePrivateTrackIdentity()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        accept(harness.hub->recommendation(), resultWith({routedItem(
            MediaEntityTypeV2::Track, QStringLiteral("track-favorite"))}, QStringLiteral("recommend")));
        const QVariantMap row = harness.adapter->recommendSongs()->get(0);
        QVERIFY(!row.contains(QStringLiteral("ref")));
        QSignalSpy succeeded(harness.hub->actions(), &MediaActionRouter::actionSucceeded);

        const QUuid requestId = harness.adapter->setFavorite(row, true);

        QVERIFY(!requestId.isNull());
        QTRY_COMPARE(succeeded.size(), 1);
        QCOMPARE(succeeded.first().first().toUuid(), requestId);
        QCOMPARE(harness.session()->property("favoriteRef").toMap(),
                 mediaRefV2ToVariantMap({QStringLiteral("adapter"), QStringLiteral("adapter/home"),
                                         QStringLiteral("home"), MediaEntityTypeV2::Track,
                                         QStringLiteral("track-favorite")}));
        QVERIFY(harness.session()->property("favoriteValue").toBool());
    }

    void playRoutesThePrivateTrackAndReturnsGeneration()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        accept(harness.hub->recommendation(), resultWith({routedItem(
            MediaEntityTypeV2::Track, QStringLiteral("track-play"))}, QStringLiteral("recommend")));
        const QVariantMap row = harness.adapter->recommendSongs()->get(0);

        const QUuid generation = harness.adapter->play(row);

        QVERIFY(!generation.isNull());
        QTRY_COMPARE(harness.sink.plays, 1);
        QCOMPARE(harness.playback->currentGeneration(), generation);
        QVERIFY(!harness.playback->currentOccurrence().isNull());
        QVERIFY(harness.playback->currentOccurrence() != generation);
        QCOMPARE(harness.sink.generation, generation);
        QCOMPARE(harness.session()->property("playRef").toMap(),
                 mediaRefV2ToVariantMap({QStringLiteral("adapter"), QStringLiteral("adapter/home"),
                                         QStringLiteral("home"), MediaEntityTypeV2::Track,
                                         QStringLiteral("track-play")}));
        QCOMPARE(mediaRefV2ToVariantMap(harness.sink.stream.media),
                 harness.session()->property("playRef").toMap());
    }

    void enqueueRoutesThePrivateTrackAndReturnsOccurrence()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        accept(harness.hub->recommendation(), resultWith({routedItem(
            MediaEntityTypeV2::Track, QStringLiteral("track-enqueue"))}, QStringLiteral("recommend")));
        const QVariantMap row = harness.adapter->recommendSongs()->get(0);

        const QUuid occurrence = harness.adapter->enqueue(row);

        QVERIFY(!occurrence.isNull());
        QCOMPARE(harness.playback->queue().size(), 1);
        const QVariantMap queued = harness.playback->queue().first().toMap();
        QCOMPARE(queued.value(QStringLiteral("occurrenceId")).toUuid(), occurrence);
        QCOMPARE(queued.value(QStringLiteral("ref")).toMap(),
                 mediaRefV2ToVariantMap({QStringLiteral("adapter"), QStringLiteral("adapter/home"),
                                         QStringLiteral("home"), MediaEntityTypeV2::Track,
                                         QStringLiteral("track-enqueue")}));
        QVERIFY(!queued.contains(QStringLiteral("metadata")));
        QVERIFY(!queued.contains(QStringLiteral("url")));
        QVERIFY(!queued.contains(QStringLiteral("headers")));
        QCOMPARE(harness.sink.prepares, 0);
        QVERIFY(harness.session()->property("playRef").toMap().isEmpty());
    }

    void selectedSourceSignalFollowsARealScopeChange()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        QSignalSpy changed(harness.adapter.get(),
                           &OriginalUiMusicAdapter::selectedSourceInstanceIdChanged);

        harness.adapter->setSelectedSourceInstanceId(QStringLiteral("adapter/home"));

        QCOMPARE(changed.size(), 1);
        QCOMPARE(harness.adapter->selectedSourceInstanceId(), QStringLiteral("adapter/home"));
        QCOMPARE(harness.hub->selectedSourceInstanceId(), QStringLiteral("adapter/home"));
    }

    void sourceOptionsSignalFollowsARealRegistryChange()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        QCOMPARE(harness.adapter->sourceOptions().size(), 2);
        QSignalSpy changed(harness.adapter.get(), &OriginalUiMusicAdapter::sourceOptionsChanged);

        QVERIFY(harness.registry.disableInstance(QStringLiteral("adapter/home")));

        QTRY_COMPARE(changed.size(), 1);
        QCOMPARE(harness.adapter->sourceOptions().size(), 1);
        QCOMPARE(harness.adapter->sourceOptions().first().toMap()
                     .value(QStringLiteral("sourceInstanceId")).toString(), QString{});
    }

    void acceptedReplacementRejectsEveryActionFromRetainedOldRows()
    {
        RoutingHarness harness;
        QVERIFY(harness.init());
        accept(harness.hub->recommendation(), resultWith({
            routedItem(MediaEntityTypeV2::Track, QStringLiteral("old-track")),
            routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("old-playlist"))},
            QStringLiteral("recommend")));
        const QVariantMap oldTrack = harness.adapter->recommendSongs()->get(0);
        const QVariantMap oldPlaylist = harness.adapter->recommendSongs()->get(1);

        accept(harness.hub->recommendation(), resultWith({
            routedItem(MediaEntityTypeV2::Track, QStringLiteral("new-track")),
            routedItem(MediaEntityTypeV2::Playlist, QStringLiteral("new-playlist"))},
            QStringLiteral("recommend")));

        QVERIFY(harness.adapter->play(oldTrack).isNull());
        QVERIFY(harness.adapter->enqueue(oldTrack).isNull());
        QVERIFY(harness.adapter->setFavorite(oldTrack, true).isNull());
        QVERIFY(!harness.adapter->browse(oldPlaylist));
        QVERIFY(!harness.adapter->enqueue(harness.adapter->recommendSongs()->get(0)).isNull());
        QVERIFY(harness.adapter->browse(harness.adapter->recommendSongs()->get(1)));
    }
};

QTEST_GUILESS_MAIN(OriginalUiMusicAdapterTest)
#endif
#include "tst_OriginalUiMusicAdapter.moc"
