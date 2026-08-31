#include "MediaBridge.h"

#include "SourceAccountController.h"
#include "IMusicSourceSession.h"
#include "SourceSessionRegistry.h"
#include "SourceTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>
#include <QtMath>

namespace {

int sanitizedLimit(int limit)
{
    return qMax(0, limit);
}

bool isContainer(MediaKind kind)
{
    return kind != MediaKind::Track;
}

} // namespace

MediaBridge::MediaBridge(SourceSessionRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_searchResults(new MediaListModel(this))
    , m_browseResults(new MediaListModel(this))
{
    connect(m_searchResults, &MediaListModel::requestStateChanged, this,
            [this] { emitRequestStateChangedFor(OperationType::Search); });
    connect(m_browseResults, &MediaListModel::requestStateChanged, this,
            [this] { emitRequestStateChangedFor(OperationType::Browse); });
    if (m_registry != nullptr) {
        connect(m_registry, &SourceSessionRegistry::sessionInvalidated, this,
                &MediaBridge::invalidate);
    }
}

MediaBridge::~MediaBridge()
{
    cancel();
}

MediaListModel *MediaBridge::searchResults() const
{
    return m_searchResults;
}

MediaListModel *MediaBridge::browseResults() const
{
    return m_browseResults;
}

MediaRequestState MediaBridge::requestState() const
{
    return m_lastOperation.has_value() ? modelFor(*m_lastOperation)->requestState()
                                       : MediaRequestState::Idle;
}

SourceAccountController *MediaBridge::accountController() const
{
    return m_accountController;
}

void MediaBridge::setAccountController(SourceAccountController *controller)
{
    m_accountController = controller;
}

void MediaBridge::search(const QString &sourceScope, const QString &keyword, int limit)
{
    cancelOperation(OperationType::Search);
    m_lastOperation = OperationType::Search;
    const std::optional<MediaId> target = mediaIdForScope(sourceScope);
    if (!target.has_value()) {
        m_searchIntent.reset();
        m_searchResults->beginRequest(QUuid::createUuid());
        m_searchResults->setFailure(
            {MediaErrorKind::InvalidRequest, QStringLiteral("A source/account scope is required"), false});
        return;
    }

    Intent intent{OperationType::Search, *target, keyword, sanitizedLimit(limit)};
    m_searchIntent = intent;
    dispatch(intent);
}

void MediaBridge::browse(const QString &sourceId, const QString &accountId, const QString &nativeId,
                         int kind, int limit)
{
    cancelOperation(OperationType::Browse);
    m_lastOperation = OperationType::Browse;
    if (sourceId.isEmpty() || accountId.isEmpty()) {
        m_browseIntent.reset();
        m_browseResults->beginRequest(QUuid::createUuid());
        m_browseResults->setFailure(
            {MediaErrorKind::InvalidRequest, QStringLiteral("A source and account are required"), false});
        return;
    }

    Intent intent{OperationType::Browse,
                  {sourceId, accountId, nativeId, static_cast<MediaKind>(kind)},
                  {},
                  sanitizedLimit(limit)};
    m_browseIntent = intent;
    dispatch(intent);
}

void MediaBridge::open(const QVariantMap &item)
{
    const MediaId id = mediaIdFromVariantMap(item);
    if (id.sourceId.isEmpty() || id.accountId.isEmpty() || id.nativeId.isEmpty()) {
        return;
    }
    if (item.value(QStringLiteral("container")).toBool() || isContainer(id.kind)) {
        browse(id.sourceId, id.accountId, id.nativeId, static_cast<int>(id.kind), 50);
    }
}

void MediaBridge::loadMore()
{
    if (!m_lastOperation.has_value()) {
        return;
    }
    const std::optional<Intent> *intent = intentFor(*m_lastOperation);
    MediaListModel *model = modelFor(*m_lastOperation);
    if (!intent->has_value() || !model->hasMore()) {
        return;
    }
    Intent next = **intent;
    next.append = true;
    dispatch(next);
}

void MediaBridge::retry()
{
    if (!m_lastOperation.has_value()) {
        return;
    }
    const std::optional<Intent> *intent = intentFor(*m_lastOperation);
    if (!intent->has_value() || !modelFor(*m_lastOperation)->canRetry()) {
        return;
    }
    dispatch(**intent);
}

void MediaBridge::cancel()
{
    cancelOperation(OperationType::Search);
    cancelOperation(OperationType::Browse);
    cancelActions();
}

void MediaBridge::loadArtwork(const QVariantMap &item)
{
    dispatchAction(ActionType::Artwork, item);
}

void MediaBridge::loadLyrics(const QVariantMap &item)
{
    dispatchAction(ActionType::Lyrics, item);
}

void MediaBridge::play(const QVariantMap &item)
{
    cancelActions(ActionType::Playback);
    dispatchAction(ActionType::Playback, item);
}

void MediaBridge::enqueue(const QVariantMap &item)
{
    const MediaId id = mediaIdFromVariantMap(item);
    if (!hasValidMediaId(id)) {
        return;
    }
    emit enqueueReady(queueEntryFromItem(item, id));
}

std::optional<MediaId> MediaBridge::mediaIdForScope(const QString &sourceScope)
{
    const int separator = sourceScope.indexOf(QLatin1Char('/'));
    if (separator <= 0 || separator == sourceScope.size() - 1) {
        return std::nullopt;
    }
    const QString sourceId = sourceScope.left(separator);
    const QString accountId = sourceScope.mid(separator + 1);
    if (sourceId.isEmpty() || accountId.isEmpty()) {
        return std::nullopt;
    }
    return MediaId{sourceId, accountId};
}

std::optional<MediaKind> MediaBridge::mediaKindFromNormalized(const QString &kind)
{
    if (kind == QStringLiteral("track")) {
        return MediaKind::Track;
    }
    if (kind == QStringLiteral("album")) {
        return MediaKind::Album;
    }
    if (kind == QStringLiteral("artist")) {
        return MediaKind::Artist;
    }
    if (kind == QStringLiteral("playlist")) {
        return MediaKind::Playlist;
    }
    if (kind == QStringLiteral("directory")) {
        return MediaKind::Directory;
    }
    return std::nullopt;
}

MediaError MediaBridge::mediaErrorFromSourceError(const SourceError &error)
{
    MediaErrorKind kind = MediaErrorKind::Unknown;
    switch (error.kind) {
    case SourceErrorKind::Network:
        kind = MediaErrorKind::Network;
        break;
    case SourceErrorKind::Authentication:
        kind = MediaErrorKind::Authentication;
        break;
    case SourceErrorKind::Authorization:
        kind = MediaErrorKind::Authorization;
        break;
    case SourceErrorKind::NotFound:
        kind = MediaErrorKind::NotFound;
        break;
    case SourceErrorKind::RateLimited:
        kind = MediaErrorKind::RateLimited;
        break;
    case SourceErrorKind::InvalidRequest:
        kind = MediaErrorKind::InvalidRequest;
        break;
    case SourceErrorKind::Unavailable:
        kind = MediaErrorKind::Unavailable;
        break;
    case SourceErrorKind::Unsupported:
        kind = MediaErrorKind::Unsupported;
        break;
    case SourceErrorKind::Unknown:
        break;
    }
    const bool retryable = kind == MediaErrorKind::Unknown || kind == MediaErrorKind::Network
        || kind == MediaErrorKind::RateLimited || kind == MediaErrorKind::Unavailable;
    return {kind, error.message, retryable};
}

MediaItem MediaBridge::mediaItemFromNormalized(const QJsonObject &item, const MediaId &provider)
{
    const std::optional<MediaKind> kind = mediaKindFromNormalized(
        item.value(QStringLiteral("kind")).toString());
    if (!kind.has_value() || item.value(QStringLiteral("id")).toString().isEmpty()) {
        return {};
    }

    MediaItem mapped;
    mapped.id = {provider.sourceId, provider.accountId, item.value(QStringLiteral("id")).toString(), *kind};
    mapped.title = item.value(QStringLiteral("title")).toString();
    const QString artist = item.value(QStringLiteral("artist")).toString();
    if (!artist.isEmpty()) {
        mapped.artists = {artist};
        mapped.subtitle = artist;
    }
    mapped.albumTitle = item.value(QStringLiteral("album")).toString();
    mapped.durationMs = qRound64(item.value(QStringLiteral("duration")).toDouble() * 1000.0);
    const QString coverArtId = item.value(QStringLiteral("coverArtId")).toString();
    if (!coverArtId.isEmpty()) {
        mapped.extra.insert(QStringLiteral("coverArtId"), coverArtId);
    }
    mapped.playable = *kind == MediaKind::Track;
    mapped.container = isContainer(*kind);
    return mapped;
}

QString MediaBridge::actionName(ActionType type)
{
    switch (type) {
    case ActionType::Artwork:
        return QStringLiteral("loadArtwork");
    case ActionType::Lyrics:
        return QStringLiteral("loadLyrics");
    case ActionType::Playback:
        return QStringLiteral("play");
    }
    return {};
}

QVariantMap MediaBridge::actionErrorMap(ActionType type, const MediaId &id, const MediaError &error)
{
    return {{QStringLiteral("action"), actionName(type)},
            {QStringLiteral("mediaId"), mediaIdToVariantMap(id)},
            {QStringLiteral("kind"), static_cast<int>(error.kind)},
            {QStringLiteral("message"), error.message},
            {QStringLiteral("retryable"), error.retryable}};
}

QVariantMap MediaBridge::queueEntryFromItem(const QVariantMap &item, const MediaId &id)
{
    const QStringList artists = item.value(QStringLiteral("artists")).toStringList();
    const QString artist = !artists.isEmpty() ? artists.join(QStringLiteral(", "))
                                              : item.value(QStringLiteral("subtitle")).toString();
    return {{QStringLiteral("mediaId"), mediaIdToVariantMap(id)},
            {QStringLiteral("title"), item.value(QStringLiteral("title")).toString()},
            {QStringLiteral("artist"), artist},
            {QStringLiteral("albumTitle"), item.value(QStringLiteral("albumTitle")).toString()},
            {QStringLiteral("artworkUrl"), item.value(QStringLiteral("artworkUrl")).toUrl().toString()},
            {QStringLiteral("durationMs"), item.value(QStringLiteral("durationMs")).toLongLong()}};
}

bool MediaBridge::hasValidMediaId(const MediaId &id)
{
    return !id.sourceId.isEmpty() && !id.accountId.isEmpty() && !id.nativeId.isEmpty();
}

MediaListModel *MediaBridge::modelFor(OperationType type) const
{
    return type == OperationType::Search ? m_searchResults : m_browseResults;
}

std::optional<MediaBridge::Intent> *MediaBridge::intentFor(OperationType type)
{
    return type == OperationType::Search ? &m_searchIntent : &m_browseIntent;
}

const std::optional<MediaBridge::Intent> *MediaBridge::intentFor(OperationType type) const
{
    return type == OperationType::Search ? &m_searchIntent : &m_browseIntent;
}

void MediaBridge::dispatch(const Intent &intent)
{
    MediaListModel *model = modelFor(intent.type);
    m_lastOperation = intent.type;
    const QUuid localRequestId = QUuid::createUuid();
    model->beginRequest(localRequestId);
    if (m_registry == nullptr) {
        model->setFailure({MediaErrorKind::Unavailable, QStringLiteral("Source registry is unavailable"), true});
        return;
    }

    IMusicSourceSession *session = m_registry->sessionFor(intent.target);
    if (session == nullptr) {
        model->setFailure(
            {MediaErrorKind::Unavailable, QStringLiteral("Source account is unavailable"), true});
        return;
    }
    connectSession(session);

    const QUuid providerRequestId = intent.type == OperationType::Search
        ? session->search({intent.keyword, intent.limit})
        : session->browse({intent.target.nativeId, {}, intent.limit});
    if (providerRequestId.isNull()) {
        model->setFailure(
            {MediaErrorKind::Unavailable, QStringLiteral("Source did not start the request"), true});
        return;
    }

    model->beginRequest(providerRequestId);
    m_pendingRequests.insert(providerRequestId, {intent, model, session});
    m_registry->trackRequest(intent.target, providerRequestId);
}

void MediaBridge::dispatchAction(ActionType type, const QVariantMap &item)
{
    const MediaId id = mediaIdFromVariantMap(item);
    if (!hasValidMediaId(id)) {
        failAction(type, id,
                   {MediaErrorKind::InvalidRequest, QStringLiteral("A complete media identity is required"),
                    false});
        return;
    }
    if (m_registry == nullptr) {
        failAction(type, id,
                   {MediaErrorKind::Unavailable, QStringLiteral("Source registry is unavailable"), true});
        return;
    }

    IMusicSourceSession *session = m_registry->sessionFor(id);
    if (session == nullptr) {
        failAction(type, id,
                   {MediaErrorKind::Unavailable, QStringLiteral("Source account is unavailable"), true});
        return;
    }
    connectSession(session);

    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();
    const QString artworkId = extra.value(QStringLiteral("coverArtId")).toString();
    const TrackRef track{id.sourceId,
                         type == ActionType::Artwork && !artworkId.isEmpty() ? artworkId : id.nativeId};
    QUuid requestId;
    switch (type) {
    case ActionType::Artwork:
        requestId = m_registry->requestArtwork(id, track);
        break;
    case ActionType::Lyrics:
        requestId = session->fetchLyrics(track);
        break;
    case ActionType::Playback:
        requestId = session->resolveStream(track);
        break;
    }
    if (requestId.isNull()) {
        failAction(type, id,
                   {MediaErrorKind::Unavailable, QStringLiteral("Source did not start the request"), true});
        return;
    }

    m_pendingActions.insert(requestId, {type, id, item, session});
    m_registry->trackRequest(id, requestId);
}

void MediaBridge::connectSession(IMusicSourceSession *session)
{
    if (session == nullptr || m_connectedSessions.contains(session)) {
        return;
    }
    m_connectedSessions.insert(session);
    connect(session, &IMusicSourceSession::requestSucceeded, this, &MediaBridge::handleSucceeded);
    connect(session, &IMusicSourceSession::requestFailed, this, &MediaBridge::handleFailed);
    connect(session, &QObject::destroyed, this, [this, session] { m_connectedSessions.remove(session); });
}

void MediaBridge::cancelOperation(OperationType type)
{
    QList<QUuid> requestIds;
    for (auto request = m_pendingRequests.cbegin(); request != m_pendingRequests.cend(); ++request) {
        if (request->intent.type == type) {
            requestIds.append(request.key());
        }
    }
    for (const QUuid &requestId : requestIds) {
        const PendingRequest pending = m_pendingRequests.take(requestId);
        if (pending.session != nullptr) {
            pending.session->cancel(requestId);
        }
        if (m_registry != nullptr) {
            m_registry->completeRequest(pending.intent.target, requestId);
        }
    }
    if (!requestIds.isEmpty()) {
        modelFor(type)->clear();
    }
}

void MediaBridge::cancelActions()
{
    const QList<QUuid> requestIds = m_pendingActions.keys();
    for (const QUuid &requestId : requestIds) {
        const PendingAction pending = m_pendingActions.take(requestId);
        if (pending.session != nullptr) {
            pending.session->cancel(requestId);
        }
        if (m_registry != nullptr) {
            m_registry->completeRequest(pending.id, requestId);
        }
    }
}

void MediaBridge::cancelActions(ActionType type)
{
    QList<QUuid> requestIds;
    for (auto action = m_pendingActions.cbegin(); action != m_pendingActions.cend(); ++action) {
        if (action->type == type) {
            requestIds.append(action.key());
        }
    }
    for (const QUuid &requestId : requestIds) {
        const PendingAction pending = m_pendingActions.take(requestId);
        if (pending.session != nullptr) {
            pending.session->cancel(requestId);
        }
        if (m_registry != nullptr) {
            m_registry->completeRequest(pending.id, requestId);
        }
    }
}

void MediaBridge::invalidate(const QString &sourceId, const QString &accountId)
{
    QList<QUuid> requestIds;
    for (auto request = m_pendingRequests.cbegin(); request != m_pendingRequests.cend(); ++request) {
        if (request->intent.target.sourceId == sourceId && request->intent.target.accountId == accountId) {
            requestIds.append(request.key());
        }
    }
    QSet<OperationType> affected;
    for (const QUuid &requestId : requestIds) {
        const PendingRequest pending = m_pendingRequests.take(requestId);
        affected.insert(pending.intent.type);
    }
    for (OperationType type : affected) {
        modelFor(type)->clear();
    }

    QList<QUuid> actionIds;
    for (auto action = m_pendingActions.cbegin(); action != m_pendingActions.cend(); ++action) {
        if (action->id.sourceId == sourceId && action->id.accountId == accountId) {
            actionIds.append(action.key());
        }
    }
    for (const QUuid &requestId : actionIds) {
        m_pendingActions.remove(requestId);
    }
}

void MediaBridge::handleSucceeded(const QUuid &requestId, const QString &operation,
                                  const QJsonValue &result)
{
    if (m_pendingActions.contains(requestId)) {
        handleActionSucceeded(requestId, operation, result);
        return;
    }
    const auto pending = m_pendingRequests.find(requestId);
    if (pending == m_pendingRequests.end()) {
        return;
    }
    const PendingRequest request = pending.value();
    m_pendingRequests.erase(pending);
    if (m_registry != nullptr) {
        m_registry->completeRequest(request.intent.target, requestId);
    }
    const QString expectedOperation = request.intent.type == OperationType::Search
        ? QStringLiteral("search")
        : QStringLiteral("browse");
    if (request.model == nullptr) {
        return;
    }
    if (operation != expectedOperation || !result.isObject()) {
        request.model->setFailure(
            {MediaErrorKind::InvalidRequest, QStringLiteral("Source returned an invalid response"), false});
        return;
    }

    MediaPage page;
    const QJsonArray normalizedItems = result.toObject().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : normalizedItems) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject normalized = value.toObject();
        const QString sourceId = normalized.value(QStringLiteral("sourceId")).toString();
        if (!sourceId.isEmpty() && sourceId != request.intent.target.sourceId) {
            continue;
        }
        MediaItem item = mediaItemFromNormalized(normalized, request.intent.target);
        if (!item.id.nativeId.isEmpty()) {
            page.items.append(item);
        }
    }
    if (request.intent.append) {
        request.model->appendPage(page);
    } else {
        request.model->replacePage(page);
    }
}

void MediaBridge::handleFailed(const QUuid &requestId, const SourceError &error)
{
    if (m_pendingActions.contains(requestId)) {
        handleActionFailed(requestId, error);
        return;
    }
    const auto pending = m_pendingRequests.find(requestId);
    if (pending == m_pendingRequests.end()) {
        return;
    }
    const PendingRequest request = pending.value();
    m_pendingRequests.erase(pending);
    if (m_registry != nullptr) {
        m_registry->completeRequest(request.intent.target, requestId);
    }
    if (request.model != nullptr) {
        request.model->setFailure(mediaErrorFromSourceError(error));
    }
}

void MediaBridge::handleActionSucceeded(const QUuid &requestId, const QString &operation,
                                        const QJsonValue &result)
{
    const auto pending = m_pendingActions.find(requestId);
    if (pending == m_pendingActions.end()) {
        return;
    }
    const PendingAction action = pending.value();
    m_pendingActions.erase(pending);
    if (m_registry != nullptr) {
        m_registry->completeRequest(action.id, requestId);
    }

    const QString expectedOperation = action.type == ActionType::Artwork
        ? QStringLiteral("fetchArtwork")
        : action.type == ActionType::Lyrics ? QStringLiteral("fetchLyrics")
                                            : QStringLiteral("resolveStream");
    if (operation != expectedOperation || !result.isObject()) {
        failAction(action.type, action.id,
                   {MediaErrorKind::InvalidRequest, QStringLiteral("Source returned an invalid response"),
                    false});
        return;
    }

    const QJsonObject object = result.toObject();
    const QJsonObject track = object.value(QStringLiteral("track")).toObject();
    const QVariantMap extra = action.item.value(QStringLiteral("extra")).toMap();
    const QString expectedTrackId = action.type == ActionType::Artwork
            && !extra.value(QStringLiteral("coverArtId")).toString().isEmpty()
        ? extra.value(QStringLiteral("coverArtId")).toString()
        : action.id.nativeId;
    if ((!track.isEmpty() && (track.value(QStringLiteral("sourceId")).toString() != action.id.sourceId
                              || track.value(QStringLiteral("nativeId")).toString()
                                  != expectedTrackId))) {
        failAction(action.type, action.id,
                   {MediaErrorKind::InvalidRequest, QStringLiteral("Source returned a different media item"),
                    false});
        return;
    }

    if (action.type == ActionType::Artwork) {
        const QString url = object.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) {
            failAction(action.type, action.id,
                       {MediaErrorKind::InvalidRequest, QStringLiteral("Source artwork URL is missing"), false});
            return;
        }
        emit artworkReady({{QStringLiteral("mediaId"), mediaIdToVariantMap(action.id)},
                           {QStringLiteral("artworkUrl"), url}});
        return;
    }

    if (action.type == ActionType::Lyrics) {
        if (!object.value(QStringLiteral("lyrics")).isString()) {
            failAction(action.type, action.id,
                       {MediaErrorKind::InvalidRequest, QStringLiteral("Source lyrics are missing"), false});
            return;
        }
        emit lyricsReady({{QStringLiteral("mediaId"), mediaIdToVariantMap(action.id)},
                          {QStringLiteral("lyrics"), object.value(QStringLiteral("lyrics")).toString()},
                          {QStringLiteral("synced"), object.value(QStringLiteral("synced")).toBool()}});
        return;
    }

    const QJsonValue headers = object.value(QStringLiteral("headers"));
    if (!headers.isUndefined() && (!headers.isObject() || !headers.toObject().isEmpty())) {
        failAction(action.type, action.id,
                   {MediaErrorKind::Unsupported,
                    QStringLiteral("Playback entries requiring HTTP headers are unsupported"), false});
        return;
    }

    QDateTime expiresAt;
    const QJsonValue expiration = object.value(QStringLiteral("expiresAt"));
    if (!expiration.isUndefined()) {
        if (expiration.isString()) {
            expiresAt = QDateTime::fromString(expiration.toString(), Qt::ISODateWithMs);
            if (!expiresAt.isValid()) {
                expiresAt = QDateTime::fromString(expiration.toString(), Qt::ISODate);
            }
        } else if (expiration.isDouble()) {
            expiresAt = QDateTime::fromMSecsSinceEpoch(qRound64(expiration.toDouble()), QTimeZone::UTC);
        }
        if (!expiresAt.isValid()) {
            failAction(action.type, action.id,
                       {MediaErrorKind::InvalidRequest, QStringLiteral("Source stream expiry is invalid"),
                        false});
            return;
        }
        expiresAt = expiresAt.toUTC();
        if (expiresAt <= QDateTime::currentDateTimeUtc()) {
            failAction(action.type, action.id,
                       {MediaErrorKind::Unavailable, QStringLiteral("Source stream has expired"), true});
            return;
        }
    }

    const QString url = object.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        failAction(action.type, action.id,
                   {MediaErrorKind::InvalidRequest, QStringLiteral("Source stream URL is missing"), false});
        return;
    }

    QVariantMap entry = queueEntryFromItem(action.item, action.id);
    entry.insert(QStringLiteral("url"), url);
    if (expiresAt.isValid()) {
        entry.insert(QStringLiteral("expiresAt"), expiresAt);
    }
    emit playbackReady(entry);
}

void MediaBridge::handleActionFailed(const QUuid &requestId, const SourceError &error)
{
    const auto pending = m_pendingActions.find(requestId);
    if (pending == m_pendingActions.end()) {
        return;
    }
    const PendingAction action = pending.value();
    m_pendingActions.erase(pending);
    if (m_registry != nullptr) {
        m_registry->completeRequest(action.id, requestId);
    }
    failAction(action.type, action.id, mediaErrorFromSourceError(error));
}

void MediaBridge::failAction(ActionType type, const MediaId &id, const MediaError &error)
{
    emit mediaActionFailed(actionErrorMap(type, id, error));
}

void MediaBridge::emitRequestStateChangedFor(OperationType type)
{
    if (m_lastOperation.has_value() && *m_lastOperation == type) {
        emit requestStateChanged();
    }
}
