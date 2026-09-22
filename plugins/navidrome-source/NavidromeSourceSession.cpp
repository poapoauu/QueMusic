#include "NavidromeSourceSession.h"
#include "NavidromeMappers.h"

#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QPointer>
#include <QTimer>

#include <climits>
#include <utility>

namespace {

ActionAvailabilityV2 available(const QString &reason = QStringLiteral("source.capability.available"))
{
    return {AvailabilityV2::Available, reason, {}};
}

ActionAvailabilityV2 unavailable(const QString &reason = QStringLiteral("source.permission.unknown"))
{
    return {AvailabilityV2::Unavailable, reason, {}};
}

const QList<SourceActionV2> allActions{
    SourceActionV2::Play, SourceActionV2::Artwork, SourceActionV2::Lyrics,
    SourceActionV2::Download, SourceActionV2::Favorite, SourceActionV2::Unfavorite,
    SourceActionV2::Rating, SourceActionV2::Scrobble,
    SourceActionV2::CreatePlaylist, SourceActionV2::UpdatePlaylist,
    SourceActionV2::DeletePlaylist, SourceActionV2::AddPlaylistTracks,
    SourceActionV2::RemovePlaylistTracks, SourceActionV2::FetchPlayQueue,
    SourceActionV2::SavePlayQueue, SourceActionV2::FetchBookmarks,
    SourceActionV2::CreateBookmark, SourceActionV2::DeleteBookmark};
const QList<SourceActionV2> implementedActions{
    SourceActionV2::Play,SourceActionV2::Artwork,SourceActionV2::Lyrics,
    SourceActionV2::Download,SourceActionV2::Favorite,SourceActionV2::Unfavorite,
    SourceActionV2::Rating,SourceActionV2::Scrobble,SourceActionV2::CreatePlaylist,SourceActionV2::UpdatePlaylist,
    SourceActionV2::DeletePlaylist,SourceActionV2::AddPlaylistTracks,
    SourceActionV2::RemovePlaylistTracks,SourceActionV2::FetchPlayQueue,
    SourceActionV2::SavePlayQueue,SourceActionV2::FetchBookmarks,
    SourceActionV2::CreateBookmark,SourceActionV2::DeleteBookmark};

QString permissionKey(SourceActionV2 action)
{
    switch (action) {
    case SourceActionV2::Favorite: return QStringLiteral("source.permission.favorite");
    case SourceActionV2::Unfavorite: return QStringLiteral("source.permission.unfavorite");
    case SourceActionV2::Rating: return QStringLiteral("source.permission.rating");
    case SourceActionV2::Scrobble: return QStringLiteral("source.permission.scrobble");
    case SourceActionV2::CreatePlaylist: return QStringLiteral("source.permission.createPlaylist");
    case SourceActionV2::UpdatePlaylist: return QStringLiteral("source.permission.updatePlaylist");
    case SourceActionV2::DeletePlaylist: return QStringLiteral("source.permission.deletePlaylist");
    case SourceActionV2::AddPlaylistTracks: return QStringLiteral("source.permission.addPlaylistTracks");
    case SourceActionV2::RemovePlaylistTracks: return QStringLiteral("source.permission.removePlaylistTracks");
    case SourceActionV2::FetchPlayQueue: return QStringLiteral("source.permission.fetchPlayQueue");
    case SourceActionV2::SavePlayQueue: return QStringLiteral("source.permission.savePlayQueue");
    case SourceActionV2::FetchBookmarks: return QStringLiteral("source.permission.fetchBookmarks");
    case SourceActionV2::CreateBookmark: return QStringLiteral("source.permission.createBookmark");
    case SourceActionV2::DeleteBookmark: return QStringLiteral("source.permission.deleteBookmark");
    case SourceActionV2::Download: return QStringLiteral("source.permission.download");
    default: return QStringLiteral("source.permission.unknown");
    }
}

SourceErrorV2 invalidResponse(const QString &messageKey)
{
    return {SourceErrorKindV2::InvalidRequest, messageKey,
            QStringLiteral("Navidrome returned an invalid response."), std::nullopt, false};
}

bool typedBool(const QJsonObject &object, const QString &key, bool *value)
{
    const QJsonValue field = object.value(key);
    if (!field.isBool())
        return false;
    *value = field.toBool();
    return true;
}

} // namespace

NavidromeSourceSession::NavidromeSourceSession(SourceConfigurationV2 configuration,
                                               QNetworkAccessManager *network, QObject *parent)
    : NavidromeSourceSession(std::move(configuration), network,
                             NavidromeApiClient::SaltGenerator{}, parent)
{
}

NavidromeSourceSession::NavidromeSourceSession(
    SourceConfigurationV2 configuration, QNetworkAccessManager *network,
    NavidromeApiClient::SaltGenerator saltGenerator, QObject *parent)
    : IMusicSourceSessionV2(parent), m_configuration(std::move(configuration))
{
    if (saltGenerator)
        m_client = new NavidromeApiClient(m_configuration, network, std::move(saltGenerator), this);
    else
        m_client = new NavidromeApiClient(m_configuration, network, this);
    connect(m_client, &NavidromeApiClient::succeeded, this,
            &NavidromeSourceSession::handleClientSuccess);
    connect(m_client, &NavidromeApiClient::failed, this,
            &NavidromeSourceSession::handleClientFailure);
    connect(m_client,&NavidromeApiClient::binarySucceeded,this,
            [this](QUuid clientId,const QString &,QByteArray bytes,QString mimeType) {
        auto pending=m_v2Requests.find(clientId); if (pending==m_v2Requests.end()) return;
        const V2Request request=pending.value(); m_v2Requests.erase(pending);
        if (request.operation==QStringLiteral("artwork"))
            emit actionCompleted(request.publicId,{SourceActionV2::Artwork,request.media,
                                  {{QStringLiteral("bytes"),bytes},{QStringLiteral("mimeType"),mimeType}}});
    });
    connect(m_client,&NavidromeApiClient::downloadSucceeded,this,
            [this](QUuid clientId,const QString &) {
        auto pending=m_v2Requests.find(clientId); if (pending==m_v2Requests.end()) return;
        const V2Request request=pending.value(); m_v2Requests.erase(pending);
        emit actionCompleted(request.publicId,{SourceActionV2::Download,request.media,
                              {{QStringLiteral("destination"),request.destination}}});
    });
}

NavidromeSourceSession::~NavidromeSourceSession()
{
    disconnect(m_client, nullptr, this, nullptr);
    for (const QUuid &id : m_v2Requests.keys())
        m_client->cancel(id);
    if (!m_openClientRequestId.isNull())
        m_client->cancel(m_openClientRequestId);
    m_configuration.secret.fill('\0');
    m_configuration.secret.clear();
}

SourceIdentityV2 NavidromeSourceSession::identity() const
{
    return {m_configuration.sourceId, m_configuration.sourceInstanceId,
            m_configuration.accountId, m_configuration.displayName};
}

SourceSessionStateV2 NavidromeSourceSession::state() const
{
    return m_state;
}

CapabilitySetV2 NavidromeSourceSession::capabilities() const
{
    return m_capabilities;
}

QUuid NavidromeSourceSession::open()
{
    const QUuid previousClientRequestId = m_openClientRequestId;
    m_openClientRequestId = {};
    m_openRequestId = {};
    m_openStage.clear();
    m_songLyricsExtension=false;
    if (!previousClientRequestId.isNull())
        m_client->cancel(previousClientRequestId);

    const QUuid requestId = QUuid::createUuid();
    m_openRequestId = requestId;
    m_openStage = QStringLiteral("ping");
    emit requestStarted(requestId);
    if (!isOpenRequestActive(requestId))
        return requestId;
    setCapabilities({});
    if (!isOpenRequestActive(requestId))
        return requestId;
    setState(SourceSessionStateV2::Connecting);
    if (!isOpenRequestActive(requestId))
        return requestId;
    const QUuid clientRequestId =
        m_client->get(QStringLiteral("open.ping"), QStringLiteral("ping"));
    if (!isOpenRequestActive(requestId)) {
        m_client->cancel(clientRequestId);
        return requestId;
    }
    m_openClientRequestId = clientRequestId;
    return requestId;
}

void NavidromeSourceSession::close()
{
    if (m_state == SourceSessionStateV2::Closed && m_openRequestId.isNull()
        && m_openClientRequestId.isNull()
        && m_v2Requests.isEmpty() && m_localV2Requests.isEmpty())
        return;
    const QUuid openClientRequestId = m_openClientRequestId;
    const QList<QUuid> v2Ids = m_v2Requests.keys();
    m_openClientRequestId = {};
    m_openRequestId = {};
    m_openStage.clear();
    m_songLyricsExtension = false;
    m_v2Requests.clear();
    m_localV2Requests.clear();
    setState(SourceSessionStateV2::Closing);
    if (!openClientRequestId.isNull())
        m_client->cancel(openClientRequestId);
    for (const QUuid &id : v2Ids)
        m_client->cancel(id);
    setCapabilities({});
    setState(SourceSessionStateV2::Closed);
}

void NavidromeSourceSession::cancel(const QUuid &requestId)
{
    if (!requestId.isNull() && requestId == m_openRequestId) {
        const QUuid clientRequestId = m_openClientRequestId;
        m_openClientRequestId = {};
        m_openRequestId = {};
        m_openStage.clear();
        if (!clientRequestId.isNull())
            m_client->cancel(clientRequestId);
        setCapabilities({});
        setState(SourceSessionStateV2::Closed);
        return;
    }
    for (auto it=m_v2Requests.begin();it!=m_v2Requests.end();) {
        if (it->publicId==requestId) {
            const QUuid clientId=it.key(); it=m_v2Requests.erase(it); m_client->cancel(clientId);
        } else ++it;
    }
    m_localV2Requests.remove(requestId);
}

namespace {
SourceErrorV2 task10Unsupported()
{
    return {SourceErrorKindV2::Unsupported,QStringLiteral("source.operation.unsupported"),
            QStringLiteral("This Navidrome operation is unavailable."),{},false};
}
}

QUuid NavidromeSourceSession::fetchPage(const PageQueryV2 &query)
{
    const QUuid id=QUuid::createUuid(); m_localV2Requests.insert(id); emit requestStarted(id);
    if (!m_localV2Requests.contains(id)) return id;
    const auto invalid=[this,id] { return scheduleV2Failure(
        {SourceErrorKindV2::InvalidRequest,QStringLiteral("source.page.invalid"),
         QStringLiteral("The Navidrome page request is invalid."),{},false},id); };
    if (query.limit<1 || query.limit>500
        || query.scope.sourceInstanceId!=m_configuration.sourceInstanceId) return invalid();
    int offset=0;
    if (!query.cursor.isEmpty()) {
        if (!QRegularExpression(QStringLiteral("^(0|[1-9][0-9]*)$")).match(query.cursor).hasMatch())
            return invalid();
        bool ok=false; const qlonglong parsed=query.cursor.toLongLong(&ok);
        if (!ok || parsed<0 || parsed>INT_MAX) return invalid();
        offset=int(parsed);
    }
    const QStringList keys{"artistId","albumId","songId","playlistId","genre"};
    QString filter;
    for (auto it=query.filters.cbegin();it!=query.filters.cend();++it) {
        if (!keys.contains(it.key()) || it->metaType().id()!=QMetaType::QString
            || it->toString().isEmpty() || !filter.isEmpty()) return invalid();
        filter=it.key();
    }
    QString endpoint; QUrlQuery parameters;
    if (!filter.isEmpty()) {
        if (filter==QStringLiteral("artistId")) endpoint=QStringLiteral("getArtist");
        else if (filter==QStringLiteral("albumId")) endpoint=QStringLiteral("getAlbum");
        else if (filter==QStringLiteral("songId")) endpoint=QStringLiteral("getSong");
        else if (filter==QStringLiteral("genre")) endpoint=QStringLiteral("getSongsByGenre");
        else endpoint=QStringLiteral("getPlaylist");
        parameters.addQueryItem(filter==QStringLiteral("genre")?QStringLiteral("genre")
                                                               :QStringLiteral("id"),
                                query.filters.value(filter).toString());
        if (endpoint==QStringLiteral("getSongsByGenre")) {
            parameters.addQueryItem(QStringLiteral("count"),QString::number(query.limit));
            parameters.addQueryItem(QStringLiteral("offset"),QString::number(offset));
        }
    } else if (query.section==PageSectionKindV2::Playlists) {
        endpoint=QStringLiteral("getPlaylists");
    } else if (query.page==MusicPageKindV2::Favorites) {
        endpoint=QStringLiteral("getStarred2");
        const auto add=[&](QString prefix,PageSectionKindV2 kind) {
            parameters.addQueryItem(prefix+QStringLiteral("Count"),
                                    QString::number(query.cursor.isEmpty() || query.section==kind
                                                        ? query.limit : 0));
            parameters.addQueryItem(prefix+QStringLiteral("Offset"),
                                    QString::number(query.section==kind?offset:0));
        };
        add(QStringLiteral("song"),PageSectionKindV2::FavoriteTracks);
        add(QStringLiteral("album"),PageSectionKindV2::FavoriteAlbums);
        add(QStringLiteral("artist"),PageSectionKindV2::FavoriteArtists);
    } else if (query.page==MusicPageKindV2::Search) {
        endpoint=QStringLiteral("search3"); parameters.addQueryItem(QStringLiteral("query"),query.searchText);
        const QList<QPair<QString,PageSectionKindV2>> categories{
            {QStringLiteral("song"),PageSectionKindV2::Tracks},
            {QStringLiteral("album"),PageSectionKindV2::Albums},
            {QStringLiteral("artist"),PageSectionKindV2::Artists}};
        if (!query.cursor.isEmpty() && query.section!=PageSectionKindV2::Tracks
            && query.section!=PageSectionKindV2::Albums
            && query.section!=PageSectionKindV2::Artists) return invalid();
        for (const auto &[prefix,kind]:categories) {
            const bool selected=query.cursor.isEmpty() || query.section==kind;
            parameters.addQueryItem(prefix+QStringLiteral("Count"),
                                    QString::number(selected?query.limit:0));
            parameters.addQueryItem(prefix+QStringLiteral("Offset"),
                                    QString::number(query.section==kind?offset:0));
        }
    } else {
        switch (query.section) {
        case PageSectionKindV2::Random: endpoint="getAlbumList2"; parameters.addQueryItem("type","random"); break;
        case PageSectionKindV2::Newest: endpoint="getAlbumList2"; parameters.addQueryItem("type","newest"); break;
        case PageSectionKindV2::RecentlyPlayed: endpoint="getAlbumList2"; parameters.addQueryItem("type","recent"); break;
        case PageSectionKindV2::FrequentlyPlayed: endpoint="getAlbumList2"; parameters.addQueryItem("type","frequent"); break;
        case PageSectionKindV2::HighestRated: endpoint="getAlbumList2"; parameters.addQueryItem("type","highest"); break;
        case PageSectionKindV2::Genres: endpoint="getGenres"; break;
        case PageSectionKindV2::Artists: endpoint="getArtists"; break;
        case PageSectionKindV2::Tracks: return scheduleV2Failure(task10Unsupported(),id);
        default: return scheduleV2Failure(task10Unsupported(),id);
        }
        if (endpoint==QStringLiteral("getAlbumList2")) {
            parameters.addQueryItem(QStringLiteral("size"),QString::number(query.limit));
            parameters.addQueryItem(QStringLiteral("offset"),QString::number(offset));
        }
    }
    m_localV2Requests.remove(id);
    const QUuid clientId=m_client->get(QStringLiteral("v2.page." )+endpoint,endpoint,parameters);
    m_v2Requests.insert(clientId,{id,QStringLiteral("page"),endpoint,query,{},{},offset});
    return id;
}
bool NavidromeSourceSession::validMedia(const MediaRefV2 &media) const
{
    return media.sourcePluginId==m_configuration.sourceId
        && media.sourceInstanceId==m_configuration.sourceInstanceId
        && media.accountId==m_configuration.accountId
        && media.entityType==MediaEntityTypeV2::Track && !media.entityId.isEmpty();
}
bool NavidromeSourceSession::validArtworkMedia(const MediaRefV2 &media) const
{
    return media.sourcePluginId==m_configuration.sourceId
        && media.sourceInstanceId==m_configuration.sourceInstanceId
        && media.accountId==m_configuration.accountId
        && (media.entityType==MediaEntityTypeV2::Track
            || media.entityType==MediaEntityTypeV2::Album)
        && !media.entityId.isEmpty();
}
QUuid NavidromeSourceSession::resolveStream(const MediaRefV2 &media)
{
    const QUuid id=QUuid::createUuid(); m_localV2Requests.insert(id); emit requestStarted(id);
    if (!m_localV2Requests.contains(id)) return id;
    if (!validMedia(media)) return scheduleV2Failure(
        {SourceErrorKindV2::InvalidRequest,QStringLiteral("source.media.invalid"),
         QStringLiteral("The Navidrome media reference is invalid."),{},false},id);
    QUrlQuery query; query.addQueryItem(QStringLiteral("id"),media.entityId);
    QUrl url; SourceErrorV2 failure;
    if (!m_client->authenticatedUrl(QStringLiteral("stream"),query,&url,&failure))
        return scheduleV2Failure(failure,id);
    QTimer::singleShot(0,this,[this,id,media,url] {
        if (!m_localV2Requests.remove(id)) return;
        StreamDescriptorV2 stream; stream.media=media; stream.url=url; stream.seekable=true;
        emit streamReady(id,stream);
    });
    return id;
}
QUuid NavidromeSourceSession::fetchArtwork(const MediaRefV2 &media)
{
    const QUuid id=QUuid::createUuid(); m_localV2Requests.insert(id); emit requestStarted(id);
    if (!m_localV2Requests.contains(id)) return id;
    if (!validArtworkMedia(media)) return scheduleV2Failure(
        {SourceErrorKindV2::InvalidRequest,QStringLiteral("source.media.invalid"),
         QStringLiteral("The Navidrome media reference is invalid."),{},false},id);
    m_localV2Requests.remove(id); QUrlQuery query; query.addQueryItem(QStringLiteral("id"),media.entityId);
    const QString endpoint=media.entityType==MediaEntityTypeV2::Track
        ? QStringLiteral("getSong") : QStringLiteral("getAlbum");
    const QUuid clientId=m_client->get(QStringLiteral("v2.artwork.resolve"),endpoint,query);
    m_v2Requests.insert(clientId,{id,QStringLiteral("artwork.resolve"),endpoint,{},media,{},0});
    return id;
}
QUuid NavidromeSourceSession::fetchLyrics(const MediaRefV2 &media)
{
    const QUuid id=QUuid::createUuid(); m_localV2Requests.insert(id); emit requestStarted(id);
    if (!m_localV2Requests.contains(id)) return id;
    if (!validMedia(media)) return scheduleV2Failure(
        {SourceErrorKindV2::InvalidRequest,QStringLiteral("source.media.invalid"),
         QStringLiteral("The Navidrome media reference is invalid."),{},false},id);
    QString endpoint; QString operation; QUrlQuery query;
    if (m_songLyricsExtension) {
        endpoint=QStringLiteral("getLyricsBySongId"); operation=QStringLiteral("lyrics.structured");
        query.addQueryItem(QStringLiteral("id"),media.entityId);
    } else if (const auto metadata=m_trackMetadata.constFind(media.entityId);
               metadata!=m_trackMetadata.cend()) {
        endpoint=QStringLiteral("getLyrics"); operation=QStringLiteral("lyrics.fallback");
        query.addQueryItem(QStringLiteral("artist"),metadata->artist);
        query.addQueryItem(QStringLiteral("title"),metadata->title);
    } else {
        endpoint=QStringLiteral("getSong"); operation=QStringLiteral("lyrics.song");
        query.addQueryItem(QStringLiteral("id"),media.entityId);
    }
    m_localV2Requests.remove(id); const QUuid clientId=m_client->get(QStringLiteral("v2.")+operation,endpoint,query);
    m_v2Requests.insert(clientId,{id,operation,endpoint,{},media,{},0});
    return id;
}
QUuid NavidromeSourceSession::download(const MediaRefV2 &media,const QUrl &destination)
{
    const QUuid id=QUuid::createUuid(); m_localV2Requests.insert(id); emit requestStarted(id);
    if (!m_localV2Requests.contains(id)) return id;
    const QString path=destination.toLocalFile(); const QFileInfo target(path);
    const QFileInfo parent(target.dir().absolutePath());
    if (!validMedia(media) || !destination.isValid() || !destination.isLocalFile()
        || !destination.host().isEmpty() || path.isEmpty() || !target.isAbsolute()
        || target.exists() || target.isSymLink() || target.fileName().isEmpty()
        || !parent.exists() || !parent.isDir())
        return scheduleV2Failure({SourceErrorKindV2::InvalidRequest,
                                  QStringLiteral("source.download.destinationInvalid"),
                                  QStringLiteral("The download destination is invalid."),{},false},id);
    m_localV2Requests.remove(id); QUrlQuery query; query.addQueryItem(QStringLiteral("id"),media.entityId);
    const QUuid clientId=m_client->downloadToFile(QStringLiteral("v2.download"),QStringLiteral("download"),query,path);
    m_v2Requests.insert(clientId,{id,QStringLiteral("download"),QStringLiteral("download"),{},media,destination,0});
    return id;
}

bool NavidromeSourceSession::validEntity(const MediaRefV2 &media, MediaEntityTypeV2 type) const
{
    const auto source=identity();
    return media.sourcePluginId==source.sourcePluginId
        && media.sourceInstanceId==source.sourceInstanceId && media.accountId==source.accountId
        && media.entityType==type && !media.entityId.trimmed().isEmpty();
}

MediaRefV2 NavidromeSourceSession::collectionSubject() const
{
    const auto source=identity();
    return {source.sourcePluginId,source.sourceInstanceId,source.accountId,MediaEntityTypeV2::Track,{}};
}

QUuid NavidromeSourceSession::startAction(SourceActionV2 action, const MediaRefV2 &subject,
    const QString &endpoint, const QUrlQuery &query, QVariantMap payload, bool valid,
    QList<SourceActionV2> attempted, bool foreign)
{
    const QUuid id=QUuid::createUuid();
    const QPointer<NavidromeSourceSession> guard(this);
    m_localV2Requests.insert(id);
    emit requestStarted(id);
    if (!guard || !m_localV2Requests.contains(id)) return id;
    if (!valid) return scheduleV2Failure({foreign ? SourceErrorKindV2::Unsupported
                                                : SourceErrorKindV2::InvalidRequest,
        QStringLiteral("source.action.invalid"),QStringLiteral("The Navidrome action is invalid."),{},false},id);
    if (attempted.isEmpty()) attempted={action};
    for (const auto attemptedAction:attempted) {
        if (!m_capabilities.accountActions.contains(attemptedAction)) continue;
        const auto availability=m_capabilities.action(attemptedAction);
        if (availability.state!=AvailabilityV2::Available)
            return scheduleV2Failure({availability.state==AvailabilityV2::Forbidden
                ? SourceErrorKindV2::Authorization : SourceErrorKindV2::Unavailable,
                permissionKey(attemptedAction),QStringLiteral("The Navidrome action is unavailable."),{},false},id);
    }
    // Keep the public request cancellable during a reentrant transport factory.
    const QUuid clientId=m_client->get(QStringLiteral("v2.action." )+endpoint,endpoint,query);
    if (!guard) return id;
    if (!m_localV2Requests.remove(id)) { m_client->cancel(clientId); return id; }
    V2Request request; request.publicId=id; request.operation=QStringLiteral("action");
    request.endpoint=endpoint; request.media=subject;
    request.result={action,subject,std::move(payload)};
    request.attemptedActions=std::move(attempted);
    m_v2Requests.insert(clientId,std::move(request));
    return id;
}

QUuid NavidromeSourceSession::scrobble(const MediaRefV2 &media, qint64 positionMs,
                                     bool submission)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"),media.entityId);
    query.addQueryItem(QStringLiteral("submission"),
                       submission ? QStringLiteral("true") : QStringLiteral("false"));
    // Protocol time is epoch milliseconds, not the playback position.
    return startAction(SourceActionV2::Scrobble,media,QStringLiteral("scrobble"),query,
        {{"positionMs",positionMs},{"submission",submission}},
        validEntity(media,MediaEntityTypeV2::Track) && positionMs>=0);
}

QUuid NavidromeSourceSession::setFavorite(const MediaRefV2 &media, bool favorite)
{
    const bool valid=validEntity(media,MediaEntityTypeV2::Track)
        || validEntity(media,MediaEntityTypeV2::Album) || validEntity(media,MediaEntityTypeV2::Artist);
    const QString key=media.entityType==MediaEntityTypeV2::Album ? QStringLiteral("albumId")
        : media.entityType==MediaEntityTypeV2::Artist ? QStringLiteral("artistId") : QStringLiteral("id");
    QUrlQuery query; query.addQueryItem(key,media.entityId);
    return startAction(favorite?SourceActionV2::Favorite:SourceActionV2::Unfavorite,media,
        favorite?QStringLiteral("star"):QStringLiteral("unstar"),query,{{"favorite",favorite}},valid);
}
QUuid NavidromeSourceSession::setRating(const MediaRefV2 &media, int rating)
{
    QUrlQuery query; query.addQueryItem("id",media.entityId); query.addQueryItem("rating",QString::number(rating));
    return startAction(SourceActionV2::Rating,media,"setRating",query,{{"rating",rating}},
        validEntity(media,MediaEntityTypeV2::Track) && rating>=0 && rating<=5);
}
QUuid NavidromeSourceSession::createPlaylist(const QString &name, const QList<MediaRefV2> &tracks)
{
    QUrlQuery query; query.addQueryItem("name",name.trimmed());
    bool foreign=false;
    for (const auto &track:tracks) {
        foreign |= !validEntity(track,MediaEntityTypeV2::Track);
        query.addQueryItem("songId",track.entityId);
    }
    auto subject=collectionSubject(); subject.entityType=MediaEntityTypeV2::Playlist;
    return startAction(SourceActionV2::CreatePlaylist,subject,"createPlaylist",query,{},
        !name.trimmed().isEmpty() && !foreign,{},foreign);
}
QUuid NavidromeSourceSession::updatePlaylist(const MediaRefV2 &playlist, const PlaylistChangeV2 &change)
{
    QUrlQuery query; query.addQueryItem("playlistId",playlist.entityId);
    QList<SourceActionV2> attempted;
    bool valid=validEntity(playlist,MediaEntityTypeV2::Playlist),foreign=false;
    if (!change.newName.isEmpty()) {
        attempted.append(SourceActionV2::UpdatePlaylist);
        valid &= !change.newName.trimmed().isEmpty(); query.addQueryItem("name",change.newName.trimmed());
    }
    if (!change.tracksToAdd.isEmpty()) attempted.append(SourceActionV2::AddPlaylistTracks);
    for (const auto &track:change.tracksToAdd) {
        foreign |= !validEntity(track,MediaEntityTypeV2::Track);
        query.addQueryItem("songIdToAdd",track.entityId);
    }
    if (!change.trackIndexesToRemove.isEmpty()) attempted.append(SourceActionV2::RemovePlaylistTracks);
    QSet<int> seen;
    for (int index:change.trackIndexesToRemove) {
        valid &= index>=0 && !seen.contains(index); seen.insert(index);
        query.addQueryItem("songIndexToRemove",QString::number(index));
    }
    return startAction(SourceActionV2::UpdatePlaylist,playlist,"updatePlaylist",query,{},
        valid && !foreign && !attempted.isEmpty(),attempted,foreign);
}
QUuid NavidromeSourceSession::deletePlaylist(const MediaRefV2 &playlist)
{
    QUrlQuery query; query.addQueryItem("id",playlist.entityId);
    return startAction(SourceActionV2::DeletePlaylist,playlist,"deletePlaylist",query,{},
        validEntity(playlist,MediaEntityTypeV2::Playlist));
}
QUuid NavidromeSourceSession::fetchPlayQueue()
{
    return startAction(SourceActionV2::FetchPlayQueue,collectionSubject(),"getPlayQueue",{});
}
QUuid NavidromeSourceSession::savePlayQueue(const QList<MediaRefV2> &items,
    const MediaRefV2 &current, qint64 positionMs)
{
    QUrlQuery query; QVariantList refs; bool valid=positionMs>=0;
    for (const auto &item:items) {
        valid &= validEntity(item,MediaEntityTypeV2::Track);
        query.addQueryItem("id",item.entityId); refs.append(mediaRefV2ToVariantMap(item));
    }
    if (!items.isEmpty()) {
        valid &= validEntity(current,MediaEntityTypeV2::Track) && items.contains(current);
        query.addQueryItem("current",current.entityId);
        query.addQueryItem("position",QString::number(positionMs));
    } else valid &= current==MediaRefV2{};
    QVariantMap payload{{"items",refs},{"current",items.isEmpty()?QVariantMap{}:mediaRefV2ToVariantMap(current)},
        {"positionMs",items.isEmpty()?qint64(0):positionMs}};
    return startAction(SourceActionV2::SavePlayQueue,collectionSubject(),"savePlayQueue",query,payload,valid);
}
QUuid NavidromeSourceSession::fetchBookmarks()
{
    return startAction(SourceActionV2::FetchBookmarks,collectionSubject(),"getBookmarks",{});
}
QUuid NavidromeSourceSession::createBookmark(const MediaRefV2 &media, qint64 positionMs,
    const QString &comment)
{
    QUrlQuery query; query.addQueryItem("id",media.entityId);
    query.addQueryItem("position",QString::number(positionMs));
    if (!comment.isEmpty()) query.addQueryItem("comment",comment);
    return startAction(SourceActionV2::CreateBookmark,media,"createBookmark",query,
        {{"positionMs",positionMs},{"comment",comment}},validEntity(media,MediaEntityTypeV2::Track) && positionMs>=0);
}
QUuid NavidromeSourceSession::deleteBookmark(const MediaRefV2 &media)
{
    QUrlQuery query; query.addQueryItem("id",media.entityId);
    return startAction(SourceActionV2::DeleteBookmark,media,"deleteBookmark",query,{},
        validEntity(media,MediaEntityTypeV2::Track));
}
void NavidromeSourceSession::finishAction(const V2Request &request, const QJsonObject &response)
{
    auto result=request.result;
    bool valid=true;
    if (result.action==SourceActionV2::CreatePlaylist) {
        const auto playlist=response.value("playlist").toObject();
        result.subject.entityId=playlist.value("id").toString();
        const QString name=playlist.value("name").toString();
        valid=!result.subject.entityId.trimmed().isEmpty() && !name.trimmed().isEmpty();
        result.payload={{"name",name}};
    } else if (result.action==SourceActionV2::FetchPlayQueue) {
        valid=NavidromeMappers::playQueue(response,identity(),&result.payload);
    } else if (result.action==SourceActionV2::FetchBookmarks) {
        valid=NavidromeMappers::bookmarks(response,identity(),&result.payload);
    }
    if (!valid) emit requestFailed(request.publicId,invalidResponse(QStringLiteral("source.action.invalidResponse")));
    else emit actionCompleted(request.publicId,result);
}

QUuid NavidromeSourceSession::scheduleV2Failure(const SourceErrorV2 &error,QUuid publicId)
{
    if (publicId.isNull()) {
        publicId=QUuid::createUuid(); m_localV2Requests.insert(publicId); emit requestStarted(publicId);
    }
    QTimer::singleShot(0,this,[this,publicId,error] {
        if (m_localV2Requests.remove(publicId)) emit requestFailed(publicId,error);
    });
    return publicId;
}

void NavidromeSourceSession::finishPage(const V2Request &request,const QJsonObject &response)
{
    PageResultV2 page;
    const SourceIdentityV2 source=identity();
    const int limit=request.pageQuery.limit;
    const auto paginate=[&](PageSectionV2 section,bool local) {
        if (local) {
            const int total=section.items.size();
            section.items=section.items.mid(request.offset,limit);
            section.hasMore=request.offset+section.items.size()<total;
        } else {
            if (section.items.size()>limit) section.items=section.items.mid(0,limit);
            section.hasMore=section.items.size()>=limit;
        }
        section.nextCursor=section.hasMore
            ? QString::number(qint64(request.offset)+section.items.size()) : QString{};
        return section;
    };
    const auto trackSection=[&](const QJsonArray &values) {
        PageSectionV2 section; section.kind=PageSectionKindV2::Tracks;
        section.sectionId=QStringLiteral("tracks"); section.titleKey=QStringLiteral("music.section.tracks");
        for (const auto &value:values) if (value.isObject())
            section.items.append(NavidromeMappers::song(value.toObject(),source));
        return section;
    };
    if (request.endpoint==QStringLiteral("getAlbumList2")) {
        page.sections={paginate(NavidromeMappers::albums(request.pageQuery.section,response,source),false)};
    } else if (request.endpoint==QStringLiteral("getGenres")) {
        PageSectionV2 section; section.kind=PageSectionKindV2::Genres;
        section.sectionId=QStringLiteral("genres"); section.titleKey=QStringLiteral("music.section.genres");
        for (const auto &value:response.value(QStringLiteral("genres")).toObject().value(QStringLiteral("genre")).toArray()) {
            if (!value.isObject()) continue; const auto object=value.toObject();
            MediaItemV2 item;
            item.ref={source.sourcePluginId,source.sourceInstanceId,source.accountId,
                      MediaEntityTypeV2::Genre,object.value(QStringLiteral("value")).toString()};
            item.title=item.ref.entityId; item.metadata.insert(QStringLiteral("sourceBadge"),source.displayName);
            section.items.append(item);
        }
        page.sections={paginate(section,true)};
    } else if (request.endpoint==QStringLiteral("getArtists")) {
        PageSectionV2 section; section.kind=PageSectionKindV2::Artists;
        section.sectionId=QStringLiteral("artists"); section.titleKey=QStringLiteral("music.section.artists");
        for (const auto &index:response.value(QStringLiteral("artists")).toObject().value(QStringLiteral("index")).toArray())
            for (const auto &artist:index.toObject().value(QStringLiteral("artist")).toArray())
                if (artist.isObject()) section.items.append(NavidromeMappers::artist(artist.toObject(),source));
        page.sections={paginate(section,true)};
    } else if (request.endpoint==QStringLiteral("getArtist")) {
        page.sections={paginate(NavidromeMappers::albums(PageSectionKindV2::Albums,response,source),true)};
    } else if (request.endpoint==QStringLiteral("getAlbum")) {
        page.sections={paginate(trackSection(response.value(QStringLiteral("album")).toObject()
                                                .value(QStringLiteral("song")).toArray()),true)};
    } else if (request.endpoint==QStringLiteral("getSong")) {
        const auto value=response.value(QStringLiteral("song"));
        page.sections={trackSection(value.isObject()?QJsonArray{value}:QJsonArray{})};
    } else if (request.endpoint==QStringLiteral("getSongsByGenre")) {
        page.sections={paginate(trackSection(response.value(QStringLiteral("songsByGenre")).toObject()
                                                .value(QStringLiteral("song")).toArray()),false)};
    } else if (request.endpoint==QStringLiteral("getPlaylists")) {
        const auto root=response.value(QStringLiteral("playlists"));
        const auto entries=root.toObject().value(QStringLiteral("playlist"));
        bool valid=root.isObject() && (entries.isUndefined() || entries.isArray());
        for (const auto &value:entries.toArray()) {
            const auto id=value.toObject().value(QStringLiteral("id"));
            valid &= value.isObject() && id.isString() && !id.toString().trimmed().isEmpty();
        }
        if (!valid) {
            emit requestFailed(request.publicId,invalidResponse(QStringLiteral("source.playlists.invalid")));
            return;
        }
        PageSectionV2 section; section.kind=PageSectionKindV2::Playlists;
        section.sectionId=QStringLiteral("playlists"); section.titleKey=QStringLiteral("music.section.playlists");
        for (const auto &value:entries.toArray())
            section.items.append(NavidromeMappers::playlist(value.toObject(),source));
        page.sections={paginate(section,true)};
    } else if (request.endpoint==QStringLiteral("getPlaylist")) {
        const auto playlist=response.value(QStringLiteral("playlist")).toObject();
        auto section=trackSection(playlist.value(QStringLiteral("entry")).toArray());
        const QString playlistId=request.pageQuery.filters.value(QStringLiteral("playlistId")).toString();
        for (int index=0;index<section.items.size();++index) {
            section.items[index].metadata.insert(QStringLiteral("playlistId"),playlistId);
            section.items[index].metadata.insert(QStringLiteral("playlistIndex"),index);
        }
        page.sections={paginate(section,true)};
    } else if (request.endpoint==QStringLiteral("getStarred2")) {
        auto sections=NavidromeMappers::starred(response,source);
        for (auto &section:sections) section=paginate(section,false);
        if (!request.pageQuery.cursor.isEmpty()) {
            for (const auto &section:sections) if (section.kind==request.pageQuery.section)
                page.sections={section};
        } else page.sections=sections;
    } else if (request.endpoint==QStringLiteral("search3")) {
        auto sections=NavidromeMappers::search(response,source);
        for (auto &section:sections) section=paginate(section,false);
        if (!request.pageQuery.cursor.isEmpty()) {
            for (const auto &section:sections) if (section.kind==request.pageQuery.section)
                page.sections={section};
        } else page.sections=sections;
    }
    emit pageReady(request.publicId,page);
}

void NavidromeSourceSession::handleClientSuccess(const QUuid &requestId,
                                                 const QString &operation,
                                                 const QJsonObject &response)
{
    auto v2=m_v2Requests.find(requestId);
    if (v2!=m_v2Requests.end()) {
        const V2Request request=v2.value(); m_v2Requests.erase(v2);
        if (request.operation==QStringLiteral("action")) {
            finishAction(request,response);
        } else if (request.operation==QStringLiteral("page")) {
            finishPage(request,response);
        } else if (request.operation==QStringLiteral("artwork.resolve")) {
            const QString key=request.media.entityType==MediaEntityTypeV2::Track
                ? QStringLiteral("song") : QStringLiteral("album");
            const QString coverArt=response.value(key).toObject()
                                       .value(QStringLiteral("coverArt")).toString();
            if (coverArt.isEmpty()) {
                emit requestFailed(request.publicId,
                                   invalidResponse(QStringLiteral("source.artwork.invalid")));
            } else {
                QUrlQuery query; query.addQueryItem(QStringLiteral("id"),coverArt);
                const QUuid next=m_client->getBinary(QStringLiteral("v2.artwork"),
                                                      QStringLiteral("getCoverArt"),query,
                                                      32ll*1024*1024);
                V2Request continuation=request; continuation.operation=QStringLiteral("artwork");
                continuation.endpoint=QStringLiteral("getCoverArt");
                m_v2Requests.insert(next,continuation);
            }
        } else if (request.operation==QStringLiteral("lyrics.song")) {
            const auto song=response.value(QStringLiteral("song")).toObject();
            const QString artist=song.value(QStringLiteral("artist")).toString();
            const QString title=song.value(QStringLiteral("title")).toString();
            if (artist.isEmpty() || title.isEmpty()) {
                emit requestFailed(request.publicId,invalidResponse(QStringLiteral("source.lyrics.invalid")));
            } else {
                m_trackMetadata.insert(request.media.entityId,{artist,title});
                QUrlQuery query; query.addQueryItem(QStringLiteral("artist"),artist);
                query.addQueryItem(QStringLiteral("title"),title);
                const QUuid next=m_client->get(QStringLiteral("v2.lyrics.fallback"),
                                                QStringLiteral("getLyrics"),query);
                V2Request continuation=request; continuation.operation=QStringLiteral("lyrics.fallback");
                continuation.endpoint=QStringLiteral("getLyrics"); m_v2Requests.insert(next,continuation);
            }
        } else {
            QString lyrics;
            if (request.operation==QStringLiteral("lyrics.structured")) {
                const auto structured=response.value(QStringLiteral("lyricsList")).toObject()
                                          .value(QStringLiteral("structuredLyrics")).toArray();
                if (!structured.isEmpty())
                    for (const auto &line:structured.first().toObject().value(QStringLiteral("line")).toArray()) {
                        const QString value=line.toObject().value(QStringLiteral("value")).toString();
                        if (!value.isNull()) { if (!lyrics.isEmpty()) lyrics+=QLatin1Char('\n'); lyrics+=value; }
                    }
            } else {
                lyrics=response.value(QStringLiteral("lyrics")).toObject()
                               .value(QStringLiteral("value")).toString();
            }
            if (lyrics.isNull()) emit requestFailed(request.publicId,
                                                      invalidResponse(QStringLiteral("source.lyrics.invalid")));
            else emit actionCompleted(request.publicId,{SourceActionV2::Lyrics,request.media,
                                      {{QStringLiteral("lyrics"),lyrics}}});
        }
        return;
    }
    if (!m_openRequestId.isNull() && requestId == m_openClientRequestId) {
        const QUuid openRequestId = m_openRequestId;
        m_openClientRequestId = {};
        if (operation == QStringLiteral("open.ping")) {
            CapabilitySetV2 next = m_capabilities;
            next.serverActions = conservativeServerActions();
            next.accountActions = unavailableAccountActions();
            setCapabilities(next);
            if (isOpenRequestActive(openRequestId))
                startExtensions(openRequestId);
        } else if (operation == QStringLiteral("open.extensions")) {
            for (const auto &value:response.value(QStringLiteral("openSubsonicExtensions")).toArray())
                if (value.toObject().value(QStringLiteral("name")).toString()==QStringLiteral("songLyrics"))
                    m_songLyricsExtension=true;
            startCurrentUser(openRequestId);
        } else if (operation == QStringLiteral("open.user")) {
            const QJsonValue userValue = response.value(QStringLiteral("user"));
            const QJsonObject user = userValue.toObject();
            const bool validUser = userValue.isObject()
                && user.value(QStringLiteral("username")).isString()
                && user.value(QStringLiteral("username")).toString()
                    == m_configuration.parameters.value(QStringLiteral("username")).toString();
            finishOpenReady(openRequestId, validUser, user);
        }
        return;
    }

}

void NavidromeSourceSession::handleClientFailure(const QUuid &requestId,
                                                 const SourceErrorV2 &error)
{
    auto v2=m_v2Requests.find(requestId);
    if (v2!=m_v2Requests.end()) {
        const V2Request request=v2.value(); m_v2Requests.erase(v2);
        if (error.kind==SourceErrorKindV2::Authorization) {
            auto attempted=request.attemptedActions;
            if (request.operation==QStringLiteral("download")) attempted={SourceActionV2::Download};
            if (!attempted.isEmpty()) {
                auto next=m_capabilities;
                for (const auto action:attempted)
                    next.accountActions[action]={AvailabilityV2::Forbidden,permissionKey(action),
                                                 next.accountAction(action).constraints};
                // Register the terminal until observers finish; close/cancel may reenter.
                m_localV2Requests.insert(request.publicId);
                const QPointer<NavidromeSourceSession> guard(this);
                setCapabilities(next);
                if (!guard || !m_localV2Requests.remove(request.publicId)) return;
            }
        }
        emit requestFailed(request.publicId,error); return;
    }
    if (!m_openRequestId.isNull() && requestId == m_openClientRequestId) {
        const QUuid openRequestId = m_openRequestId;
        const QString stage = m_openStage;
        m_openClientRequestId = {};
        if (error.kind == SourceErrorKindV2::Unsupported
            || error.kind == SourceErrorKindV2::Authorization
            || error.kind == SourceErrorKindV2::InvalidRequest) {
            // Optional negotiation stages are identified from the server/account
            // layers already established after ping.
            if (!m_capabilities.serverActions.isEmpty()) {
                if (stage == QStringLiteral("user"))
                    finishOpenReady(openRequestId, false);
                else if (stage == QStringLiteral("extensions"))
                    startCurrentUser(openRequestId);
                return;
            }
        }
        failOpen(openRequestId, error);
        return;
    }}

bool NavidromeSourceSession::isOpenRequestActive(const QUuid &requestId) const
{
    return !requestId.isNull() && m_openRequestId == requestId;
}

void NavidromeSourceSession::startExtensions(const QUuid &requestId)
{
    if (!isOpenRequestActive(requestId))
        return;
    m_openStage = QStringLiteral("extensions");
    const QUuid clientRequestId = m_client->get(
        QStringLiteral("open.extensions"), QStringLiteral("getOpenSubsonicExtensions"));
    if (!isOpenRequestActive(requestId)) {
        m_client->cancel(clientRequestId);
        return;
    }
    m_openClientRequestId = clientRequestId;
}

void NavidromeSourceSession::startCurrentUser(const QUuid &requestId)
{
    if (!isOpenRequestActive(requestId))
        return;
    m_openStage = QStringLiteral("user");
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("username"),
                       m_configuration.parameters.value(QStringLiteral("username")).toString());
    const QUuid clientRequestId =
        m_client->get(QStringLiteral("open.user"), QStringLiteral("getUser"), query);
    if (!isOpenRequestActive(requestId)) {
        m_client->cancel(clientRequestId);
        return;
    }
    m_openClientRequestId = clientRequestId;
}

void NavidromeSourceSession::finishOpenReady(const QUuid &requestId, bool validUser,
                                             const QJsonObject &user)
{
    if (!isOpenRequestActive(requestId))
        return;
    CapabilitySetV2 next = m_capabilities;
    next.accountActions = validUser ? accountActions(user) : unavailableAccountActions();
    setCapabilities(next);
    if (!isOpenRequestActive(requestId))
        return;
    m_openClientRequestId = {};
    m_openRequestId = {};
    m_openStage.clear();
    setState(SourceSessionStateV2::Ready);
}

void NavidromeSourceSession::failOpen(const QUuid &requestId, const SourceErrorV2 &error)
{
    if (!isOpenRequestActive(requestId))
        return;
    m_openClientRequestId = {};
    m_openRequestId = {};
    m_openStage.clear();
    setState(error.kind == SourceErrorKindV2::Authentication
                 ? SourceSessionStateV2::AuthenticationRequired
                 : SourceSessionStateV2::Failed);
    emit requestFailed(requestId, error);
}

void NavidromeSourceSession::setState(SourceSessionStateV2 state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void NavidromeSourceSession::setCapabilities(const CapabilitySetV2 &capabilities)
{
    if (m_capabilities.serverActions == capabilities.serverActions
        && m_capabilities.accountActions == capabilities.accountActions)
        return;
    m_capabilities = capabilities;
    emit capabilitiesChanged(m_capabilities);
}

QHash<SourceActionV2, ActionAvailabilityV2>
NavidromeSourceSession::conservativeServerActions()
{
    QHash<SourceActionV2, ActionAvailabilityV2> result;
    for (SourceActionV2 action : allActions)
        result.insert(action, implementedActions.contains(action)
                                  ? available()
                                  : ActionAvailabilityV2{AvailabilityV2::Unsupported,{},{}});
    return result;
}

QHash<SourceActionV2, ActionAvailabilityV2>
NavidromeSourceSession::unavailableAccountActions()
{
    QHash<SourceActionV2, ActionAvailabilityV2> result;
    for (SourceActionV2 action : allActions)
        result.insert(action, implementedActions.contains(action)
                                  ? unavailable()
                                  : ActionAvailabilityV2{AvailabilityV2::Unsupported,{},{}});
    return result;
}

QHash<SourceActionV2, ActionAvailabilityV2>
NavidromeSourceSession::accountActions(const QJsonObject &user)
{
    QHash<SourceActionV2, ActionAvailabilityV2> result;
    for (SourceActionV2 action : allActions)
        result.insert(action, implementedActions.contains(action)
                                  ? available(QStringLiteral("source.permission.available"))
                                  : ActionAvailabilityV2{AvailabilityV2::Unsupported,{},{}});
    const auto applyRole = [&result, &user](const QString &field,
                                            const QList<SourceActionV2> &actions) {
        bool granted = false;
        const bool typed = typedBool(user, field, &granted);
        for (SourceActionV2 action : actions)
            result.insert(action, typed && granted
                                      ? available(QStringLiteral("source.permission.available"))
                                      : unavailable(QStringLiteral("source.permission.denied")));
    };
    applyRole(QStringLiteral("streamRole"), {SourceActionV2::Play});
    applyRole(QStringLiteral("coverArtRole"), {SourceActionV2::Artwork});
    applyRole(QStringLiteral("downloadRole"), {SourceActionV2::Download});
    applyRole(QStringLiteral("playlistRole"), {SourceActionV2::CreatePlaylist,
        SourceActionV2::UpdatePlaylist, SourceActionV2::DeletePlaylist,
        SourceActionV2::AddPlaylistTracks, SourceActionV2::RemovePlaylistTracks});
    return result;
}
