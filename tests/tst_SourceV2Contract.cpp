#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <type_traits>
#include <utility>

namespace {

template<typename Type, typename = void>
struct HasSettingsSchema : std::false_type { };

template<typename Type>
struct HasSettingsSchema<Type,
                         std::void_t<decltype(std::declval<const Type &>().settingsSchema())>>
    : std::true_type { };

class FakeMusicSourceSessionV2 final : public IMusicSourceSessionV2,
                                       public IPageProviderV2,
                                       public IFavoriteProviderV2,
                                       public IPlaybackProviderV2,
                                       public IRatingProviderV2,
                                       public IScrobbleProviderV2,
                                       public IPlaylistProviderV2,
                                       public IDownloadProviderV2,
                                       public IPlayQueueProviderV2,
                                       public IBookmarkProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IFavoriteProviderV2 IPlaybackProviderV2 IRatingProviderV2
                     IScrobbleProviderV2 IPlaylistProviderV2 IDownloadProviderV2
                         IPlayQueueProviderV2 IBookmarkProviderV2)

public:
    using IMusicSourceSessionV2::IMusicSourceSessionV2;

    SourceIdentityV2 identity() const override
    {
        return {QStringLiteral("fake"), QStringLiteral("home"),
                QStringLiteral("admin"), QStringLiteral("Fake Home")};
    }

    SourceSessionStateV2 state() const override { return SourceSessionStateV2::Ready; }

    CapabilitySetV2 capabilities() const override
    {
        return {{{SourceActionV2::Play, {AvailabilityV2::Available, {}, {}}}}};
    }

    QUuid open() override { return QUuid::createUuid(); }
    void close() override { }
    void cancel(const QUuid &) override { }

    QUuid fetchPage(const PageQueryV2 &) override
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId] {
            emit pageReady(requestId, PageResultV2{});
        });
        return requestId;
    }

    QUuid setFavorite(const MediaRefV2 &, bool) override { return nextRequestId(); }
    QUuid resolveStream(const MediaRefV2 &) override { return nextRequestId(); }
    QUuid fetchArtwork(const MediaRefV2 &) override { return nextRequestId(); }
    QUuid fetchLyrics(const MediaRefV2 &) override { return nextRequestId(); }
    QUuid setRating(const MediaRefV2 &, int) override { return nextRequestId(); }
    QUuid scrobble(const MediaRefV2 &, qint64, bool) override { return nextRequestId(); }
    QUuid createPlaylist(const QString &, const QList<MediaRefV2> &) override
    {
        return nextRequestId();
    }
    QUuid updatePlaylist(const MediaRefV2 &, const PlaylistChangeV2 &) override
    {
        return nextRequestId();
    }
    QUuid deletePlaylist(const MediaRefV2 &) override { return nextRequestId(); }
    QUuid download(const MediaRefV2 &, const QUrl &) override { return nextRequestId(); }
    QUuid fetchPlayQueue() override { return nextRequestId(); }
    QUuid savePlayQueue(const QList<MediaRefV2> &, const MediaRefV2 &, qint64) override
    {
        return nextRequestId();
    }
    QUuid fetchBookmarks() override { return nextRequestId(); }
    QUuid createBookmark(const MediaRefV2 &, qint64, const QString &) override
    {
        return nextRequestId();
    }
    QUuid deleteBookmark(const MediaRefV2 &) override { return nextRequestId(); }

private:
    static QUuid nextRequestId() { return QUuid::createUuid(); }
};

class FakeMusicSourcePluginV2 final : public QObject,
                                      public IMusicSourcePluginV2,
                                      public IPluginSettingsProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IMusicSourcePluginV2 IPluginSettingsProviderV2)

public:
    SourceDescriptorV2 descriptor() const override
    {
        return {QStringLiteral("org.quemusic.fake"), QStringLiteral("fake"),
                QStringLiteral("Fake Source"), QStringLiteral("1.0.0"),
                QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI, {}};
    }

    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &, QObject *parent) override
    {
        return new FakeMusicSourceSessionV2(parent);
    }

    SettingsSchemaV2 settingsSchema() const override
    {
        return {{QStringLiteral("connection"), QStringLiteral("source.settings.connection"),
                 {{QStringLiteral("url"), QStringLiteral("source.settings.url"),
                   SettingsFieldTypeV2::Url, true, false, {}, {}, {}}}}};
    }
};

static_assert(!HasSettingsSchema<IMusicSourcePluginV2>::value);
static_assert(!std::is_base_of_v<QObject, IPageProviderV2>);
static_assert(!std::is_base_of_v<QObject, IPluginSettingsProviderV2>);

} // namespace

class SourceV2ContractTest : public QObject {
    Q_OBJECT

private slots:
    void locksStableContractIdentifiers();
    void discoversOptionalProvidersByInterface();
    void returnsCompletionsThroughBaseSessionSignals();
    void keepsSettingsSchemaOnOptionalProvider();
};

void SourceV2ContractTest::locksStableContractIdentifiers()
{
    QCOMPARE(QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI, 2);
    QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID),
             QStringLiteral("org.quemusic.MusicSourcePlugin/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PAGE_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.PageProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLAYBACK_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.PlaybackProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_FAVORITE_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.FavoriteProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_RATING_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.RatingProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_SCROBBLE_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.ScrobbleProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLAYLIST_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.PlaylistProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_DOWNLOAD_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.DownloadProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLAY_QUEUE_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.PlayQueueProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_BOOKMARK_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.BookmarkProvider/2.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLUGIN_SETTINGS_PROVIDER_V2_IID),
             QStringLiteral("org.quemusic.source.PluginSettingsProvider/2.0"));
}

void SourceV2ContractTest::discoversOptionalProvidersByInterface()
{
    FakeMusicSourceSessionV2 session;

    QVERIFY(qobject_cast<IPageProviderV2 *>(&session));
    QVERIFY(qobject_cast<IFavoriteProviderV2 *>(&session));
    QVERIFY(qobject_cast<IPlaybackProviderV2 *>(&session));
    QVERIFY(qobject_cast<IRatingProviderV2 *>(&session));
    QVERIFY(qobject_cast<IScrobbleProviderV2 *>(&session));
    QVERIFY(qobject_cast<IPlaylistProviderV2 *>(&session));
    QVERIFY(qobject_cast<IDownloadProviderV2 *>(&session));
    QVERIFY(qobject_cast<IPlayQueueProviderV2 *>(&session));
    QVERIFY(qobject_cast<IBookmarkProviderV2 *>(&session));
}

void SourceV2ContractTest::returnsCompletionsThroughBaseSessionSignals()
{
    FakeMusicSourceSessionV2 session;
    QSignalSpy pageReady(&session, &IMusicSourceSessionV2::pageReady);
    auto *pageProvider = qobject_cast<IPageProviderV2 *>(&session);

    QVERIFY(pageProvider);
    const QUuid requestId = pageProvider->fetchPage(PageQueryV2{});

    QVERIFY(!requestId.isNull());
    QVERIFY(pageReady.wait(1000));
    QCOMPARE(pageReady.constFirst().at(0).toUuid(), requestId);
    QVERIFY(QMetaType::fromType<SourceSessionStateV2>().isValid());
    QVERIFY(QMetaType::fromType<CapabilitySetV2>().isValid());
    QVERIFY(QMetaType::fromType<PageResultV2>().isValid());
    QVERIFY(QMetaType::fromType<StreamDescriptorV2>().isValid());
    QVERIFY(QMetaType::fromType<ActionResultV2>().isValid());
    QVERIFY(QMetaType::fromType<SourceErrorV2>().isValid());
}

void SourceV2ContractTest::keepsSettingsSchemaOnOptionalProvider()
{
    FakeMusicSourcePluginV2 plugin;
    IMusicSourcePluginV2 *basePlugin = &plugin;

    QCOMPARE(basePlugin->descriptor().sdkAbi, 2);
    auto *settingsProvider = qobject_cast<IPluginSettingsProviderV2 *>(&plugin);
    QVERIFY(settingsProvider);
    QCOMPARE(settingsProvider->settingsSchema().constFirst().fields.constFirst().type,
             SettingsFieldTypeV2::Url);

    const SourceConfigurationV2 configuration{
        QStringLiteral("org.quemusic.fake"), QStringLiteral("fake"),
        QStringLiteral("home"), QStringLiteral("admin"), QStringLiteral("Fake Home"), {}, {}};
    IMusicSourceSessionV2 *session = basePlugin->createSession(configuration, &plugin);
    QVERIFY(session);
    QCOMPARE(session->parent(), &plugin);
}

QTEST_MAIN(SourceV2ContractTest)
#include "tst_SourceV2Contract.moc"
