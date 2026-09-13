#include "MusicHub.h"
#include "MusicPageModel.h"
#include "OnlineListModel.h"
#include "OriginalUiMusicAdapter.h"
#include "SourceScopeStore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

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
        QCOMPARE(adapter.fullItem(row).value(QStringLiteral("ref")).toMap()
                     .value(QStringLiteral("entityId")), QStringLiteral("42"));
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
        QCOMPARE(adapter.fullItem(first).value(QStringLiteral("ref")).toMap().value(QStringLiteral("sourceInstanceId")), QStringLiteral("nas-a"));
        QCOMPARE(adapter.fullItem(second).value(QStringLiteral("ref")).toMap().value(QStringLiteral("sourceInstanceId")), QStringLiteral("nas-b"));
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
        QCOMPARE(adapter.fullItem(adapter.favoriteSongs()->get(0)).value(QStringLiteral("ref")).toMap()
                     .value(QStringLiteral("entityType")).toInt(), int(MediaEntityTypeV2::Track));
        QCOMPARE(adapter.fullItem(adapter.favoriteLists()->get(0)).value(QStringLiteral("ref")).toMap()
                     .value(QStringLiteral("entityType")).toInt(), int(MediaEntityTypeV2::Playlist));
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
        QCOMPARE(adapter.fullItem(adapter.recommendSongs()->get(0)).value(QStringLiteral("ref")).toMap()
                     .value(QStringLiteral("entityId")), QStringLiteral("new"));
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

        QVERIFY(adapter.fullItem(unknown).isEmpty());
        QVERIFY(!adapter.browse(unknown));
        QVERIFY(adapter.play(unknown).isNull());
        QVERIFY(adapter.enqueue(unknown).isNull());
        QVERIFY(adapter.setFavorite(unknown, true).isNull());
    }
};

QTEST_GUILESS_MAIN(OriginalUiMusicAdapterTest)
#include "tst_OriginalUiMusicAdapter.moc"
