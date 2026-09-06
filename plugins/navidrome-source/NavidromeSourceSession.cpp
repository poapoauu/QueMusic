#include "NavidromeSourceSession.h"

#include <QJsonArray>
#include <QTimer>

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

const QList<SourceActionV2> playlistActions{
    SourceActionV2::CreatePlaylist, SourceActionV2::UpdatePlaylist,
    SourceActionV2::DeletePlaylist, SourceActionV2::AddPlaylistTracks,
    SourceActionV2::RemovePlaylistTracks};

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
}

NavidromeSourceSession::~NavidromeSourceSession()
{
    disconnect(m_client, nullptr, this, nullptr);
    const QList<QUuid> legacyIds = m_legacyRequests.keys();
    for (const QUuid &id : legacyIds)
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
        && m_legacyRequests.isEmpty() && m_localLegacyRequests.isEmpty())
        return;
    const QUuid openClientRequestId = m_openClientRequestId;
    const QList<QUuid> legacyIds = m_legacyRequests.keys();
    m_openClientRequestId = {};
    m_openRequestId = {};
    m_openStage.clear();
    m_legacyRequests.clear();
    m_localLegacyRequests.clear();
    m_lyricsTracks.clear();
    setState(SourceSessionStateV2::Closing);
    if (!openClientRequestId.isNull())
        m_client->cancel(openClientRequestId);
    for (const QUuid &id : legacyIds)
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
    m_lyricsTracks.remove(requestId);
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

void NavidromeSourceSession::handleClientSuccess(const QUuid &requestId,
                                                 const QString &operation,
                                                 const QJsonObject &response)
{
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
            // Every current v2 action has a standard Subsonic endpoint; Lyrics
            // also has a standard fallback. Extensions therefore add no grants
            // in Task 9, and the conservative post-ping action map stays intact.
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
        result.insert(action, available());
    return result;
}

QHash<SourceActionV2, ActionAvailabilityV2>
NavidromeSourceSession::unavailableAccountActions()
{
    QHash<SourceActionV2, ActionAvailabilityV2> result;
    for (SourceActionV2 action : allActions)
        result.insert(action, unavailable());
    return result;
}

QHash<SourceActionV2, ActionAvailabilityV2>
NavidromeSourceSession::accountActions(const QJsonObject &user)
{
    QHash<SourceActionV2, ActionAvailabilityV2> result;
    for (SourceActionV2 action : allActions)
        result.insert(action, available(QStringLiteral("source.permission.available")));
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
    applyRole(QStringLiteral("playlistRole"), playlistActions);
    return result;
}
