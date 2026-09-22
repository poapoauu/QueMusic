#include "NavidromeSmokeState.h"

#include <QTest>

class NavidromeSmokeStateTest : public QObject {
    Q_OBJECT

private slots:
    void cleanupDeletesPlaylistBeforeRestoringFavorite();
    void cleanupRestoresOriginalFavoriteState();
    void cleanupFailureDoesNotRetryForever();
};

void NavidromeSmokeStateTest::cleanupDeletesPlaylistBeforeRestoringFavorite()
{
    NavidromeSmokeState state;
    state.recordInitialFavorite(false);
    state.recordFavoriteMutation();
    state.recordPlaylistCreated({QStringLiteral("navidrome"), QStringLiteral("instance"),
                                 QStringLiteral("account"), MediaEntityTypeV2::Playlist,
                                 QStringLiteral("playlist-1")});

    QCOMPARE(state.nextCleanupAction(), SmokeCleanupAction::DeletePlaylist);
    state.recordPlaylistDeleted();
    QCOMPARE(state.nextCleanupAction(), SmokeCleanupAction::RestoreFavorite);
    state.recordFavoriteRestored();
    QCOMPARE(state.nextCleanupAction(), SmokeCleanupAction::None);
}

void NavidromeSmokeStateTest::cleanupRestoresOriginalFavoriteState()
{
    NavidromeSmokeState state;
    state.recordInitialFavorite(true);
    state.recordFavoriteMutation();

    QCOMPARE(state.nextCleanupAction(), SmokeCleanupAction::RestoreFavorite);
    QVERIFY(state.initialFavorite());
}

void NavidromeSmokeStateTest::cleanupFailureDoesNotRetryForever()
{
    NavidromeSmokeState state;
    state.recordInitialFavorite(false);
    state.recordFavoriteMutation();
    state.recordCleanupFailure(SmokeCleanupAction::RestoreFavorite);

    QCOMPARE(state.nextCleanupAction(), SmokeCleanupAction::None);
}

QTEST_MAIN(NavidromeSmokeStateTest)
#include "tst_NavidromeSmokeState.moc"
