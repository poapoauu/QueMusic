#if defined(QUEMUSIC_ORIGINAL_UI_ADAPTER_FIXTURE)
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

class OriginalUiAdapterSession final : public IMusicSourceSessionV2,
                                       public IPageProviderV2,
                                       public IFavoriteProviderV2,
                                       public IPlaybackProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IFavoriteProviderV2 IPlaybackProviderV2)
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
                                  SourceActionV2::Unfavorite}) {
            result.serverActions.insert(action, {AvailabilityV2::Available, {}, {}});
            result.accountActions.insert(action, {AvailabilityV2::Available, {}, {}});
        }
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
                                    {QStringLiteral("scope"), query.scope.sourceInstanceId},
                                    {QStringLiteral("filters"), query.filters}});
        setProperty("pageRequests", requests);
        emit requestStarted(id);
        PageResultV2 result;
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
        emit requestStarted(id);
        emit actionCompleted(id, {favorite ? SourceActionV2::Favorite : SourceActionV2::Unfavorite,
                                  media, {{QStringLiteral("favorite"), favorite}}});
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
    QUuid fetchArtwork(const MediaRefV2 &) override { return {}; }
    QUuid fetchLyrics(const MediaRefV2 &) override { return {}; }

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
                                  SourceActionV2::Unfavorite})
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
                              SourceActionV2::Unfavorite})
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

    bool init()
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
    IMusicSourceSessionV2 *session() { return registry.sessionFor(QStringLiteral("adapter/home")); }
};

} // namespace

class OriginalUiMusicAdapterTest final : public QObject {
    Q_OBJECT
private slots:
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
        QCOMPARE(row.value(QStringLiteral("source")), QStringLiteral("navidrome"));
        QVERIFY(!row.contains(QStringLiteral("ref")));
        QVERIFY(!row.contains(QStringLiteral("url")));
        QVERIFY(!row.contains(QStringLiteral("headers")));
        QVERIFY(!row.contains(QStringLiteral("metadata")));
        QVERIFY(!row.contains(QStringLiteral("availableActions")));
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
        accept(harness.hub->recommendation(), resultWith({routedItem(
            MediaEntityTypeV2::Playlist, QStringLiteral("playlist-42"))}, QStringLiteral("recommend")));
        const QVariantMap row = harness.adapter->recommendSongs()->get(0);

        QVERIFY(harness.adapter->browse(row));

        QTRY_COMPARE(harness.session()->property("pageRequests").toList().size(), 1);
        const QVariantMap request = harness.session()->property("pageRequests").toList().first().toMap();
        QCOMPARE(request.value(QStringLiteral("scope")), QStringLiteral("adapter/home"));
        QCOMPARE(request.value(QStringLiteral("filters")).toMap(),
                 (QVariantMap{{QStringLiteral("playlistId"), QStringLiteral("playlist-42")}}));
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
