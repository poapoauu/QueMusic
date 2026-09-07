#include "NavidromeSourceSession.h"
#include "NavidromeMappers.h"

#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
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
    SourceActionV2::Download};

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
    const QList<QUuid> legacyIds = m_legacyRequests.keys();
    for (const QUuid &id : legacyIds)
        m_client->cancel(id);
    for (const QUuid &id : m_v2Requests.keys())
        m_client->cancel(id);
    if (!m_openClientRequestId.isNull())
        m_client->cancel(m_openClientRequestId);
    m_configuration.secret.fill('\0');
    m_configuration.secret.clear();
}

SourceIdentityV2 NavidromeSourceSession::identity() const
{
    return {m_configuration.pluginPackageId, m_configuration.sourceInstanceId,
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
        && m_legacyRequests.isEmpty() && m_localLegacyRequests.isEmpty()
        && m_v2Requests.isEmpty() && m_localV2Requests.isEmpty())
        return;
    const QUuid openClientRequestId = m_openClientRequestId;
    const QList<QUuid> legacyIds = m_legacyRequests.keys();
    const QList<QUuid> v2Ids = m_v2Requests.keys();
    m_openClientRequestId = {};
    m_openRequestId = {};
    m_openStage.clear();
    m_songLyricsExtension = false;
    m_legacyRequests.clear();
    m_localLegacyRequests.clear();
    m_v2Requests.clear();
    m_localV2Requests.clear();
    m_lyricsTracks.clear();
    setState(SourceSessionStateV2::Closing);
    if (!openClientRequestId.isNull())
        m_client->cancel(openClientRequestId);
    for (const QUuid &id : legacyIds)
        m_client->cancel(id);
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
    if (m_localLegacyRequests.remove(requestId)) {
        m_lyricsTracks.remove(requestId);
        return;
    }
    QList<QUuid> clientIds;
    for (auto it = m_legacyRequests.cbegin(); it != m_legacyRequests.cend(); ++it)
        if (it->publicId == requestId)
            clientIds.append(it.key());
    for (const QUuid &clientId : clientIds) {
        m_legacyRequests.remove(clientId);
        m_client->cancel(clientId);
    }
    for (auto it=m_v2Requests.begin();it!=m_v2Requests.end();) {
        if (it->publicId==requestId) {
            const QUuid clientId=it.key(); it=m_v2Requests.erase(it); m_client->cancel(clientId);
        } else ++it;
    }
    m_lyricsTracks.remove(requestId);
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
        for (const QString &prefix:{QStringLiteral("song"),QStringLiteral("album"),QStringLiteral("artist")}) {
            parameters.addQueryItem(prefix+QStringLiteral("Count"),QString::number(query.limit));
            parameters.addQueryItem(prefix+QStringLiteral("Offset"),QString::number(offset));
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
    return media.sourcePluginId==m_configuration.pluginPackageId
        && media.sourceInstanceId==m_configuration.sourceInstanceId
        && media.accountId==m_configuration.accountId
        && media.entityType==MediaEntityTypeV2::Track && !media.entityId.isEmpty();
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
    if (!validMedia(media)) return scheduleV2Failure(
        {SourceErrorKindV2::InvalidRequest,QStringLiteral("source.media.invalid"),
         QStringLiteral("The Navidrome media reference is invalid."),{},false},id);
    m_localV2Requests.remove(id); QUrlQuery query; query.addQueryItem(QStringLiteral("id"),media.entityId);
    const QUuid clientId=m_client->getBinary(QStringLiteral("v2.artwork"),QStringLiteral("getCoverArt"),
                                              query,32ll*1024*1024);
    m_v2Requests.insert(clientId,{id,QStringLiteral("artwork"),QStringLiteral("getCoverArt"),{},media,{},0});
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
        || target.exists() || target.fileName().isEmpty() || !parent.exists() || !parent.isDir())
        return scheduleV2Failure({SourceErrorKindV2::InvalidRequest,
                                  QStringLiteral("source.download.destinationInvalid"),
                                  QStringLiteral("The download destination is invalid."),{},false},id);
    m_localV2Requests.remove(id); QUrlQuery query; query.addQueryItem(QStringLiteral("id"),media.entityId);
    const QUuid clientId=m_client->downloadToFile(QStringLiteral("v2.download"),QStringLiteral("download"),query,path);
    m_v2Requests.insert(clientId,{id,QStringLiteral("download"),QStringLiteral("download"),{},media,destination,0});
    return id;
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

QUuid NavidromeSourceSession::ping()
{
    return startLegacy(QStringLiteral("ping"), QStringLiteral("ping"));
}

QUuid NavidromeSourceSession::search(const SearchQuery &query)
{
    QUrlQuery parameters;
    const int limit = qMax(0, query.limit);
    parameters.addQueryItem(QStringLiteral("query"), query.query);
    parameters.addQueryItem(QStringLiteral("songCount"), QString::number(limit));
    parameters.addQueryItem(QStringLiteral("albumCount"), QString::number(limit));
    parameters.addQueryItem(QStringLiteral("artistCount"), QString::number(limit));
    return startLegacy(QStringLiteral("search"), QStringLiteral("search3"), parameters);
}

QUuid NavidromeSourceSession::browse(const BrowseQuery &query)
{
    QUrlQuery parameters;
    if (query.path.isEmpty())
        return startLegacy(QStringLiteral("browse"), QStringLiteral("getIndexes"), parameters);
    parameters.addQueryItem(QStringLiteral("id"), query.path);
    return startLegacy(QStringLiteral("browse"), QStringLiteral("getMusicDirectory"), parameters);
}

QUuid NavidromeSourceSession::resolveStream(const TrackRef &track)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), track.nativeId);
    QUrl url;
    SourceErrorV2 error;
    if (!m_client->authenticatedUrl(QStringLiteral("stream"), query, &url, &error))
        return scheduleLegacyFailure(QStringLiteral("resolveStream"), error);
    return scheduleLegacySuccess(
        QStringLiteral("resolveStream"),
        QJsonObject{{QStringLiteral("track"),
                     QJsonObject{{QStringLiteral("sourceId"), track.sourceId},
                                 {QStringLiteral("nativeId"), track.nativeId}}},
                    {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                    {QStringLiteral("seekable"), true}});
}

QUuid NavidromeSourceSession::fetchArtwork(const TrackRef &track)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), track.nativeId);
    QUrl url;
    SourceErrorV2 error;
    if (!m_client->authenticatedUrl(QStringLiteral("getCoverArt"), query, &url, &error))
        return scheduleLegacyFailure(QStringLiteral("fetchArtwork"), error);
    return scheduleLegacySuccess(
        QStringLiteral("fetchArtwork"),
        QJsonObject{{QStringLiteral("track"),
                     QJsonObject{{QStringLiteral("sourceId"), track.sourceId},
                                 {QStringLiteral("nativeId"), track.nativeId}}},
                    {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}});
}

QUuid NavidromeSourceSession::fetchLyrics(const TrackRef &track)
{
    const QUuid publicId = QUuid::createUuid();
    m_lyricsTracks.insert(publicId, track);
    const auto metadata = m_trackMetadata.constFind(track.nativeId);
    QUrlQuery query;
    if (metadata != m_trackMetadata.cend()) {
        query.addQueryItem(QStringLiteral("artist"), metadata->artist);
        query.addQueryItem(QStringLiteral("title"), metadata->title);
        return startLegacy(QStringLiteral("fetchLyrics"), QStringLiteral("getLyrics"), query,
                           publicId);
    }
    query.addQueryItem(QStringLiteral("id"), track.nativeId);
    return startLegacy(QStringLiteral("fetchLyrics"), QStringLiteral("getSong"), query, publicId);
}

QUuid NavidromeSourceSession::startLegacy(const QString &operation, const QString &endpoint,
                                          QUrlQuery query, QUuid publicId)
{
    if (publicId.isNull())
        publicId = QUuid::createUuid();
    const QUuid clientId = m_client->get(operation + QLatin1Char('.') + endpoint, endpoint,
                                         std::move(query));
    m_legacyRequests.insert(clientId, {publicId, operation, endpoint});
    return publicId;
}

QUuid NavidromeSourceSession::scheduleLegacySuccess(const QString &operation,
                                                    const QJsonValue &result)
{
    const QUuid id = QUuid::createUuid();
    m_localLegacyRequests.insert(id);
    QTimer::singleShot(0, this, [this, id, operation, result] {
        if (!m_localLegacyRequests.remove(id))
            return;
        emit legacyRequestSucceeded(id, operation, result);
    });
    return id;
}

QUuid NavidromeSourceSession::scheduleLegacyFailure(const QString &operation,
                                                    const SourceErrorV2 &error)
{
    Q_UNUSED(operation)
    const QUuid id = QUuid::createUuid();
    m_localLegacyRequests.insert(id);
    QTimer::singleShot(0, this, [this, id, error] {
        if (!m_localLegacyRequests.remove(id))
            return;
        emit legacyRequestFailed(id, error);
    });
    return id;
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
        if (!sections.isEmpty()) page.sections={paginate(sections.takeFirst(),false)};
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
        if (request.operation==QStringLiteral("page")) {
            finishPage(request,response);
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

    auto pending = m_legacyRequests.find(requestId);
    if (pending == m_legacyRequests.end())
        return;
    const LegacyRequest request = pending.value();
    m_legacyRequests.erase(pending);
    if (request.operation == QStringLiteral("search")) {
        const QJsonValue resultValue = response.value(QStringLiteral("searchResult3"));
        if (!resultValue.isObject()) {
            emit legacyRequestFailed(request.publicId,
                                     invalidResponse(QStringLiteral("source.search.invalid")));
            return;
        }
        QJsonArray items;
        const auto append = [this, &items](const QJsonArray &values, const QString &kind) {
            for (const QJsonValue &value : values) {
                const QJsonObject item = value.toObject();
                QJsonObject normalized{{QStringLiteral("kind"), kind},
                                       {QStringLiteral("id"), item.value(QStringLiteral("id"))},
                                       {QStringLiteral("sourceId"), QStringLiteral("navidrome")}};
                if (kind == QStringLiteral("track")) {
                    normalized.insert(QStringLiteral("title"), item.value(QStringLiteral("title")));
                    normalized.insert(QStringLiteral("artist"), item.value(QStringLiteral("artist")));
                    normalized.insert(QStringLiteral("album"), item.value(QStringLiteral("album")));
                    normalized.insert(QStringLiteral("duration"), item.value(QStringLiteral("duration")));
                    normalized.insert(QStringLiteral("coverArtId"), item.value(QStringLiteral("coverArt")));
                    const QString artist = item.value(QStringLiteral("artist")).toString();
                    const QString title = item.value(QStringLiteral("title")).toString();
                    if (!artist.isEmpty() && !title.isEmpty())
                        m_trackMetadata.insert(item.value(QStringLiteral("id")).toString(),
                                               {artist, title});
                } else {
                    normalized.insert(QStringLiteral("title"), item.value(QStringLiteral("name")));
                    normalized.insert(QStringLiteral("artist"), item.value(QStringLiteral("artist")));
                }
                items.append(normalized);
            }
        };
        const QJsonObject result = resultValue.toObject();
        append(result.value(QStringLiteral("song")).toArray(), QStringLiteral("track"));
        append(result.value(QStringLiteral("album")).toArray(), QStringLiteral("album"));
        append(result.value(QStringLiteral("artist")).toArray(), QStringLiteral("artist"));
        emit legacyRequestSucceeded(request.publicId, request.operation,
                                    QJsonObject{{QStringLiteral("items"), items}});
        return;
    }
    if (request.operation == QStringLiteral("browse")) {
        QJsonArray items;
        if (request.stage == QStringLiteral("getIndexes")) {
            const QJsonObject indexes = response.value(QStringLiteral("indexes")).toObject();
            if (indexes.isEmpty()) {
                emit legacyRequestFailed(request.publicId,
                                         invalidResponse(QStringLiteral("source.browse.invalid")));
                return;
            }
            for (const QJsonValue &value : indexes.value(QStringLiteral("artist")).toArray()) {
                const QJsonObject artist = value.toObject();
                items.append(QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("artist")},
                    {QStringLiteral("id"), artist.value(QStringLiteral("id"))},
                    {QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                    {QStringLiteral("title"), artist.value(QStringLiteral("name"))}});
            }
        } else {
            const QJsonObject directory = response.value(QStringLiteral("directory")).toObject();
            if (directory.isEmpty()) {
                emit legacyRequestFailed(request.publicId,
                                         invalidResponse(QStringLiteral("source.browse.invalid")));
                return;
            }
            for (const QJsonValue &value : directory.value(QStringLiteral("child")).toArray()) {
                const QJsonObject child = value.toObject();
                const bool directoryChild = child.value(QStringLiteral("isDir")).toBool();
                items.append(QJsonObject{{QStringLiteral("kind"),
                                          directoryChild ? QStringLiteral("directory")
                                                         : QStringLiteral("track")},
                                         {QStringLiteral("id"), child.value(QStringLiteral("id"))},
                                         {QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                                         {QStringLiteral("title"), child.value(QStringLiteral("title"))},
                                         {QStringLiteral("artist"), child.value(QStringLiteral("artist"))}});
            }
        }
        emit legacyRequestSucceeded(request.publicId, request.operation,
                                    QJsonObject{{QStringLiteral("items"), items}});
        return;
    }
    if (request.operation == QStringLiteral("fetchLyrics")) {
        const TrackRef track = m_lyricsTracks.value(request.publicId);
        if (request.stage == QStringLiteral("getSong")) {
            const QJsonObject song = response.value(QStringLiteral("song")).toObject();
            const QString artist = song.value(QStringLiteral("artist")).toString();
            const QString title = song.value(QStringLiteral("title")).toString();
            if (song.isEmpty() || artist.isEmpty() || title.isEmpty()) {
                m_lyricsTracks.remove(request.publicId);
                emit legacyRequestFailed(request.publicId,
                                         invalidResponse(QStringLiteral("source.lyrics.invalid")));
                return;
            }
            m_trackMetadata.insert(track.nativeId, {artist, title});
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("artist"), artist);
            query.addQueryItem(QStringLiteral("title"), title);
            startLegacy(request.operation, QStringLiteral("getLyrics"), query, request.publicId);
            return;
        }
        const QJsonObject lyrics = response.value(QStringLiteral("lyrics")).toObject();
        m_lyricsTracks.remove(request.publicId);
        if (lyrics.isEmpty()) {
            emit legacyRequestFailed(request.publicId,
                                     invalidResponse(QStringLiteral("source.lyrics.invalid")));
            return;
        }
        emit legacyRequestSucceeded(
            request.publicId, request.operation,
            QJsonObject{{QStringLiteral("track"),
                         QJsonObject{{QStringLiteral("sourceId"), track.sourceId},
                                     {QStringLiteral("nativeId"), track.nativeId}}},
                        {QStringLiteral("lyrics"), lyrics.value(QStringLiteral("value"))},
                        {QStringLiteral("synced"), lyrics.value(QStringLiteral("synced"))}});
        return;
    }
    emit legacyRequestSucceeded(request.publicId, request.operation, response);
}

void NavidromeSourceSession::handleClientFailure(const QUuid &requestId,
                                                 const SourceErrorV2 &error)
{
    auto v2=m_v2Requests.find(requestId);
    if (v2!=m_v2Requests.end()) {
        const QUuid publicId=v2->publicId; m_v2Requests.erase(v2);
        emit requestFailed(publicId,error); return;
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
    }
    auto pending = m_legacyRequests.find(requestId);
    if (pending == m_legacyRequests.end())
        return;
    const LegacyRequest request = pending.value();
    m_legacyRequests.erase(pending);
    m_lyricsTracks.remove(request.publicId);
    emit legacyRequestFailed(request.publicId, error);
}

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
    return result;
}
