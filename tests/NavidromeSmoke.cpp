#include "NavidromeSourceSession.h"
#include "NavidromeSmokeState.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QTextStream>
#include <QTimer>

#include <functional>

namespace {
enum class Stage { Opening, Recommendation, Stream, FavoriteProbe, FavoriteMutate,
                   FavoriteRestore, PlaylistCreate, PlaylistUpdate, PlaylistDelete,
                   CleanupPlaylist, CleanupFavorite };
QString stageName(Stage stage)
{
    switch (stage) {
    case Stage::Opening: return QStringLiteral("ping");
    case Stage::Recommendation: return QStringLiteral("recommendation");
    case Stage::Stream: return QStringLiteral("stream-resolution");
    case Stage::FavoriteProbe: return QStringLiteral("favorite-state-read");
    case Stage::FavoriteMutate: return QStringLiteral("favorite-mutation");
    case Stage::FavoriteRestore: return QStringLiteral("favorite-restore");
    case Stage::PlaylistCreate: return QStringLiteral("playlist-create");
    case Stage::PlaylistUpdate: return QStringLiteral("playlist-update");
    case Stage::PlaylistDelete: return QStringLiteral("playlist-delete");
    case Stage::CleanupPlaylist: return QStringLiteral("cleanup-playlist");
    case Stage::CleanupFavorite: return QStringLiteral("cleanup-favorite");
    }
    return QStringLiteral("unknown");
}
bool enabled(QByteArray value)
{
    value = value.trimmed().toLower();
    return value == "1" || value == "true" || value == "yes";
}
bool available(const CapabilitySetV2 &capabilities, SourceActionV2 action)
{
    return capabilities.serverAction(action).state == AvailabilityV2::Available
        && capabilities.accountAction(action).state == AvailabilityV2::Available;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout), errors(stderr);
    const QByteArray url = qgetenv("QUEMUSIC_NAVIDROME_URL");
    const QByteArray user = qgetenv("QUEMUSIC_NAVIDROME_USER");
    QByteArray password = qgetenv("QUEMUSIC_NAVIDROME_PASSWORD");
    const QByteArray favoriteId = qgetenv("QUEMUSIC_NAVIDROME_FAVORITE_TRACK_ID");
    const bool favoriteRoundTrip =
        enabled(qgetenv("QUEMUSIC_NAVIDROME_ENABLE_FAVORITE_ROUNDTRIP"));
    if (url.isEmpty() || user.isEmpty() || password.isEmpty()
        || (favoriteRoundTrip && favoriteId.isEmpty())) {
        errors << "missing required Navidrome smoke environment\n";
        password.fill('\0');
        return 64;
    }

    SourceConfigurationV2 configuration{
        QStringLiteral("org.quemusic.source.navidrome"), QStringLiteral("navidrome"),
        QStringLiteral("navidrome/smoke"), QStringLiteral("smoke"),
        QStringLiteral("Navidrome smoke"),
        {{QStringLiteral("serverUrl"), QString::fromUtf8(url)},
         {QStringLiteral("username"), QString::fromUtf8(user)},
         {QStringLiteral("quality"), QStringLiteral("original")}}, password};
    password.fill('\0'); password.clear();

    QNetworkAccessManager network;
    NavidromeSourceSession session(std::move(configuration), &network);
    auto *pages = qobject_cast<IPageProviderV2 *>(&session);
    auto *playback = qobject_cast<IPlaybackProviderV2 *>(&session);
    auto *favorites = qobject_cast<IFavoriteProviderV2 *>(&session);
    auto *playlists = qobject_cast<IPlaylistProviderV2 *>(&session);
    if (!pages || !playback || !favorites || !playlists) {
        errors << "required v2 provider interface is unavailable\n";
        return 1;
    }

    Stage stage = Stage::Opening;
    QUuid pending;
    MediaRefV2 streamTrack, favoriteTrack, playlist;
    NavidromeSmokeState smokeState;
    bool cleaningUp = false;
    SmokeCleanupAction cleanupAction = SmokeCleanupAction::None;
    QElapsedTimer elapsed; elapsed.start();
    QTimer timeout;
    timeout.setSingleShot(true);
    QTimer cleanupTimeout;
    cleanupTimeout.setSingleShot(true);
    std::function<void()> runNextCleanup;
    std::function<void(const QString &)> fail;
    const auto start = [&](Stage next, const QUuid &request) -> bool {
        stage = next; pending = request;
        if (!pending.isNull())
            return true;
        fail(QStringLiteral("null-request"));
        return false;
    };
    const auto createPlaylist = [&] {
        const QString name = QStringLiteral("QueMusic smoke %1")
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        start(Stage::PlaylistCreate, playlists->createPlaylist(name, {}));
    };

    runNextCleanup = [&] {
        cleanupAction = smokeState.nextCleanupAction();
        if (cleanupAction == SmokeCleanupAction::None) {
            cleanupTimeout.stop();
            session.close();
            app.exit(1);
            return;
        }
        if (cleanupAction == SmokeCleanupAction::DeletePlaylist) {
            stage = Stage::CleanupPlaylist;
            pending = playlists->deletePlaylist(smokeState.playlist());
        } else {
            stage = Stage::CleanupFavorite;
            pending = favorites->setFavorite(favoriteTrack, smokeState.initialFavorite());
        }
        if (pending.isNull()) {
            errors << stageName(stage) << " failed kind=null-request elapsedMs="
                   << elapsed.elapsed() << '\n';
            smokeState.recordCleanupFailure(cleanupAction);
            QTimer::singleShot(0, &app, runNextCleanup);
        }
    };
    fail = [&](const QString &kind) {
        if (cleaningUp)
            return;
        cleaningUp = true;
        errors << stageName(stage) << " failed kind=" << kind
               << " elapsedMs=" << elapsed.elapsed() << '\n';
        timeout.stop();
        cleanupTimeout.start(5000);
        runNextCleanup();
    };

    QObject::connect(&session, &IMusicSourceSessionV2::requestFailed, &app,
        [&](const QUuid &id, const SourceErrorV2 &error) {
            if (id != pending)
                return;
            if (!cleaningUp) {
                fail(QString::number(static_cast<int>(error.kind)));
                return;
            }
            errors << stageName(stage) << " failed kind="
                   << static_cast<int>(error.kind) << " elapsedMs=" << elapsed.elapsed() << '\n';
            smokeState.recordCleanupFailure(cleanupAction);
            QTimer::singleShot(0, &app, runNextCleanup);
        });
    QObject::connect(&session, &IMusicSourceSessionV2::stateChanged, &app,
        [&](SourceSessionStateV2 state) {
            if (stage != Stage::Opening || state != SourceSessionStateV2::Ready) return;
            const CapabilitySetV2 capabilities = session.capabilities();
            const QList<SourceActionV2> requiredActions{
                SourceActionV2::Play, SourceActionV2::CreatePlaylist,
                SourceActionV2::UpdatePlaylist, SourceActionV2::DeletePlaylist};
            for (const SourceActionV2 action : requiredActions) {
                if (!available(capabilities, action)) {
                    fail(QStringLiteral("required-capability-unavailable"));
                    return;
                }
            }
            if (favoriteRoundTrip
                && (!available(capabilities, SourceActionV2::Favorite)
                    || !available(capabilities, SourceActionV2::Unfavorite))) {
                fail(QStringLiteral("favorite-capability-unavailable"));
                return;
            }
            output << "ping ok elapsedMs=" << elapsed.elapsed() << '\n';
            PageQueryV2 query; query.page = MusicPageKindV2::Recommendation;
            query.section = PageSectionKindV2::RecentlyPlayed;
            query.scope.sourceInstanceId = QStringLiteral("navidrome/smoke"); query.limit = 10;
            start(Stage::Recommendation, pages->fetchPage(query));
        });
    QObject::connect(&session, &IMusicSourceSessionV2::pageReady, &app,
        [&](const QUuid &id, const PageResultV2 &page) {
            if (id != pending || cleaningUp) return;
            if (stage == Stage::FavoriteProbe) {
                bool initiallyFavorite = false;
                QString nextCursor;
                for (const auto &section : page.sections) {
                    if (section.kind != PageSectionKindV2::FavoriteTracks)
                        continue;
                    for (const auto &item : section.items)
                        if (item.ref.entityId == favoriteTrack.entityId) {
                            initiallyFavorite = true;
                            break;
                        }
                    if (!initiallyFavorite && section.hasMore)
                        nextCursor = section.nextCursor;
                }
                if (!initiallyFavorite && !nextCursor.isEmpty()) {
                    PageQueryV2 query;
                    query.page = MusicPageKindV2::Favorites;
                    query.section = PageSectionKindV2::FavoriteTracks;
                    query.scope.sourceInstanceId = QStringLiteral("navidrome/smoke");
                    query.cursor = nextCursor;
                    query.limit = 500;
                    start(Stage::FavoriteProbe, pages->fetchPage(query));
                    return;
                }
                smokeState.recordInitialFavorite(initiallyFavorite);
                smokeState.recordFavoriteMutation();
                start(Stage::FavoriteMutate,
                      favorites->setFavorite(favoriteTrack, !initiallyFavorite));
                return;
            }
            if (stage != Stage::Recommendation) return;
            for (const auto &section : page.sections) {
                for (const auto &item : section.items)
                    if (item.ref.entityType == MediaEntityTypeV2::Track) {
                        streamTrack = item.ref; break;
                    }
                if (!streamTrack.entityId.isEmpty()) break;
            }
            if (streamTrack.entityId.isEmpty()) { fail(QStringLiteral("no-test-track")); return; }
            output << "recommendation ok elapsedMs=" << elapsed.elapsed() << '\n';
            start(Stage::Stream, playback->resolveStream(streamTrack));
        });
    QObject::connect(&session, &IMusicSourceSessionV2::streamReady, &app,
        [&](const QUuid &id, const StreamDescriptorV2 &stream) {
            if (stage != Stage::Stream || id != pending) return;
            if (!stream.url.isValid() || stream.url.isEmpty()) {
                fail(QStringLiteral("invalid-stream")); return;
            }
            output << "stream-resolution ok elapsedMs=" << elapsed.elapsed() << '\n';
            if (favoriteRoundTrip) {
                favoriteTrack = streamTrack;
                favoriteTrack.entityId = QString::fromUtf8(favoriteId);
                PageQueryV2 query;
                query.page = MusicPageKindV2::Favorites;
                query.section = PageSectionKindV2::FavoriteTracks;
                query.scope.sourceInstanceId = QStringLiteral("navidrome/smoke");
                query.limit = 500;
                start(Stage::FavoriteProbe, pages->fetchPage(query));
            } else createPlaylist();
        });
    QObject::connect(&session, &IMusicSourceSessionV2::actionCompleted, &app,
        [&](const QUuid &id, const ActionResultV2 &result) {
            if (id != pending) return;
            if (cleaningUp) {
                output << stageName(stage) << " ok elapsedMs=" << elapsed.elapsed() << '\n';
                if (cleanupAction == SmokeCleanupAction::DeletePlaylist)
                    smokeState.recordPlaylistDeleted();
                else
                    smokeState.recordFavoriteRestored();
                QTimer::singleShot(0, &app, runNextCleanup);
                return;
            }
            output << stageName(stage) << " ok elapsedMs=" << elapsed.elapsed() << '\n';
            switch (stage) {
            case Stage::FavoriteMutate:
                start(Stage::FavoriteRestore,
                      favorites->setFavorite(favoriteTrack, smokeState.initialFavorite()));
                break;
            case Stage::FavoriteRestore:
                smokeState.recordFavoriteRestored();
                createPlaylist();
                break;
            case Stage::PlaylistCreate: {
                playlist = result.subject;
                if (playlist.entityId.isEmpty()) { fail(QStringLiteral("missing-playlist-id")); return; }
                smokeState.recordPlaylistCreated(playlist);
                PlaylistChangeV2 change; change.newName = QStringLiteral("QueMusic smoke updated");
                start(Stage::PlaylistUpdate, playlists->updatePlaylist(playlist, change)); break;
            }
            case Stage::PlaylistUpdate:
                start(Stage::PlaylistDelete, playlists->deletePlaylist(playlist)); break;
            case Stage::PlaylistDelete:
                smokeState.recordPlaylistDeleted();
                session.close();
                app.exit(0);
                break;
            default: fail(QStringLiteral("unexpected-terminal")); break;
            }
        });

    QObject::connect(&timeout, &QTimer::timeout, &app,
                     [&] { fail(QStringLiteral("timeout")); });
    QObject::connect(&cleanupTimeout, &QTimer::timeout, &app, [&] {
        errors << "cleanup failed kind=timeout elapsedMs=" << elapsed.elapsed() << '\n';
        session.close();
        app.exit(1);
    });
    timeout.start(60000);
    start(Stage::Opening, session.open());
    return app.exec();
}
