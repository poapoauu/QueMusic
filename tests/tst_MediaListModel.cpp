#include "MediaListModel.h"

#include <QTest>
#include <QUuid>

namespace {

MediaItem track()
{
    return {{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
             MediaKind::Track},
            QStringLiteral("A Song"), QStringLiteral("A Subtitle"),
            {QStringLiteral("First Artist"), QStringLiteral("Second Artist")},
            QStringLiteral("An Album"), 245000, QUrl(QStringLiteral("https://example.invalid/artwork")),
            true, false, {{QStringLiteral("rating"), 5}}};
}

QHash<int, QByteArray> expectedRoles()
{
    return {
        {MediaListModel::SourceIdRole, "sourceId"},
        {MediaListModel::AccountIdRole, "accountId"},
        {MediaListModel::NativeIdRole, "nativeId"},
        {MediaListModel::KindRole, "kind"},
        {MediaListModel::TitleRole, "title"},
        {MediaListModel::SubtitleRole, "subtitle"},
        {MediaListModel::ArtistsRole, "artists"},
        {MediaListModel::AlbumTitleRole, "albumTitle"},
        {MediaListModel::DurationMsRole, "durationMs"},
        {MediaListModel::ArtworkUrlRole, "artworkUrl"},
        {MediaListModel::PlayableRole, "playable"},
        {MediaListModel::ContainerRole, "container"},
        {MediaListModel::ExtraRole, "extra"},
    };
}

}

class MediaListModelTest : public QObject {
    Q_OBJECT

private slots:
    void exposesFixedQmlRolesForMediaItems();
    void tracksLoadingReadyEmptyAndFailedStatesWithoutChangingRoles();
};

void MediaListModelTest::exposesFixedQmlRolesForMediaItems()
{
    MediaListModel model;
    const MediaItem item = track();

    model.replacePage({{item}, QStringLiteral("next-page"), true});

    QCOMPARE(model.roleNames(), expectedRoles());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), MediaListModel::SourceIdRole), QStringLiteral("navidrome"));
    QCOMPARE(model.data(model.index(0, 0), MediaListModel::AccountIdRole), QStringLiteral("home"));
    QCOMPARE(model.data(model.index(0, 0), MediaListModel::NativeIdRole), QStringLiteral("song-1"));
    QCOMPARE(model.data(model.index(0, 0), MediaListModel::KindRole),
             static_cast<int>(MediaKind::Track));
    QCOMPARE(model.data(model.index(0, 0), MediaListModel::ArtistsRole), item.artists);
    QCOMPARE(model.data(model.index(0, 0), MediaListModel::ExtraRole), item.extra);
    QCOMPARE(model.get(0).value(QStringLiteral("sourceId")), QStringLiteral("navidrome"));
    QCOMPARE(model.get(0).value(QStringLiteral("accountId")), QStringLiteral("home"));
    QCOMPARE(model.get(0).value(QStringLiteral("nativeId")), QStringLiteral("song-1"));
    QCOMPARE(model.get(0).value(QStringLiteral("kind")), static_cast<int>(MediaKind::Track));
    QCOMPARE(model.get(0).value(QStringLiteral("artists")).toStringList(), item.artists);
    QCOMPARE(model.requestState(), MediaRequestState::Ready);
    QVERIFY(model.hasMore());
}

void MediaListModelTest::tracksLoadingReadyEmptyAndFailedStatesWithoutChangingRoles()
{
    MediaListModel model;
    const QHash<int, QByteArray> roles = model.roleNames();

    model.beginRequest(QUuid::createUuid());
    QCOMPARE(model.requestState(), MediaRequestState::Loading);
    QCOMPARE(model.roleNames(), roles);

    model.replacePage({{track()}, QStringLiteral("next-page"), true});
    QCOMPARE(model.requestState(), MediaRequestState::Ready);
    QCOMPARE(model.roleNames(), roles);

    model.beginRequest(QUuid::createUuid());
    model.replacePage({{}, {}, false});
    QCOMPARE(model.requestState(), MediaRequestState::Empty);
    QCOMPARE(model.roleNames(), roles);

    const MediaError error{MediaErrorKind::Network, QStringLiteral("Network unavailable")};
    model.beginRequest(QUuid::createUuid());
    model.setFailure(error);
    QCOMPARE(model.requestState(), MediaRequestState::Failed);
    QCOMPARE(model.errorKind(), MediaErrorKind::Network);
    QCOMPARE(model.errorMessage(), QStringLiteral("Network unavailable"));
    QVERIFY(model.canRetry());
    QCOMPARE(model.roleNames(), roles);
}

QTEST_MAIN(MediaListModelTest)
#include "tst_MediaListModel.moc"
