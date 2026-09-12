#include "NavidromeSourceSession.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QTextStream>
#include <QTimer>

namespace {
enum class Stage { Opening, Recommendation, Stream, Favorite, Unfavorite,
                   PlaylistCreate, PlaylistUpdate, PlaylistDelete };
QString stageName(Stage stage)
{
    switch (stage) {
    case Stage::Opening: return QStringLiteral("ping");
    case Stage::Recommendation: return QStringLiteral("recommendation");
    case Stage::Stream: return QStringLiteral("stream-resolution");
    case Stage::Favorite: return QStringLiteral("favorite");
    case Stage::Unfavorite: return QStringLiteral("unfavorite");
    case Stage::PlaylistCreate: return QStringLiteral("playlist-create");
    case Stage::PlaylistUpdate: return QStringLiteral("playlist-update");
    case Stage::PlaylistDelete: return QStringLiteral("playlist-delete");
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
    QElapsedTimer elapsed; elapsed.start();
    const auto fail = [&](const QString &kind) {
        errors << stageName(stage) << " failed kind=" << kind
               << " elapsedMs=" << elapsed.elapsed() << '\n';
        app.exit(1);
    };
    const auto start = [&](Stage next, const QUuid &request) {
        stage = next; pending = request;
        if (pending.isNull()) fail(QStringLiteral("null-request"));
    };
    const auto createPlaylist = [&] {
        const QString name = QStringLiteral("QueMusic smoke %1")
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        start(Stage::PlaylistCreate, playlists->createPlaylist(name, {}));
    };

    QObject::connect(&session, &IMusicSourceSessionV2::requestFailed, &app,
        [&](const QUuid &id, const SourceErrorV2 &error) {
            if (id == pending) fail(QString::number(static_cast<int>(error.kind)));
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
            if (stage != Stage::Recommendation || id != pending) return;
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
                start(Stage::Favorite, favorites->setFavorite(favoriteTrack, true));
            } else createPlaylist();
        });
    QObject::connect(&session, &IMusicSourceSessionV2::actionCompleted, &app,
        [&](const QUuid &id, const ActionResultV2 &result) {
            if (id != pending) return;
            output << stageName(stage) << " ok elapsedMs=" << elapsed.elapsed() << '\n';
            switch (stage) {
            case Stage::Favorite:
                start(Stage::Unfavorite, favorites->setFavorite(favoriteTrack, false)); break;
            case Stage::Unfavorite: createPlaylist(); break;
            case Stage::PlaylistCreate: {
                playlist = result.subject;
                if (playlist.entityId.isEmpty()) { fail(QStringLiteral("missing-playlist-id")); return; }
                PlaylistChangeV2 change; change.newName = QStringLiteral("QueMusic smoke updated");
                start(Stage::PlaylistUpdate, playlists->updatePlaylist(playlist, change)); break;
            }
            case Stage::PlaylistUpdate:
                start(Stage::PlaylistDelete, playlists->deletePlaylist(playlist)); break;
            case Stage::PlaylistDelete: session.close(); app.exit(0); break;
            default: fail(QStringLiteral("unexpected-terminal")); break;
            }
        });

    QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app,
                     [&] { fail(QStringLiteral("timeout")); });
    timeout.start(60000);
    start(Stage::Opening, session.open());
    return app.exec();
}
