#include "KugouSourceSession.h"
#include "KugouMappers.h"
#include "KugouCredentialRecord.h"
#include "v2/SourceSecretsV2.h"
#include <QJsonArray>
#include <QTimer>
#include <cmath>
#include <climits>

namespace {
SourceErrorV2 failure(SourceErrorKindV2 kind, const char *key)
{
    return {kind, QString::fromLatin1(key), {}, {}, false};
}
}
KugouSourceSession::KugouSourceSession(SourceConfigurationV2 configuration, QObject *parent)
    : IMusicSourceSessionV2(parent), m_configuration(std::move(configuration)),
      m_client(new KugouApiClient(this))
{
    bindClient();
}
KugouSourceSession::KugouSourceSession(SourceConfigurationV2 configuration,
                                      QNetworkAccessManager *network, QObject *parent)
    : IMusicSourceSessionV2(parent), m_configuration(std::move(configuration)),
      m_client(new KugouApiClient(network, this))
{
    bindClient();
}
void KugouSourceSession::bindClient()
{
    connect(m_client, &KugouApiClient::succeeded, this, &KugouSourceSession::succeeded);
    connect(m_client, &KugouApiClient::failed, this, [this](QUuid clientId, SourceErrorV2 error) {
        const auto id = m_clientIds.take(clientId);
        if (!id.isNull()) fail(id, error);
    });
}
KugouSourceSession::~KugouSourceSession() { clearRequests(); }
SourceIdentityV2 KugouSourceSession::identity() const
{
    return {m_configuration.sourceId, m_configuration.sourceInstanceId,
            m_configuration.accountId, m_configuration.displayName};
}
bool KugouSourceSession::validConfiguration() const
{
    if (m_configuration.pluginPackageId != "org.quemusic.source.kugou"
        || m_configuration.sourceId != "kugou"
        || !KugouQrProtocol::validScope(m_configuration.sourceInstanceId, m_configuration.accountId)
        || !m_configuration.parameters.isEmpty()) return false;
    if (m_configuration.secret.isEmpty()) return true;
    const auto secrets = decodeSourceSecretsV2(m_configuration.secret);
    if (!secrets || secrets->size() != 1 || !secrets->contains(QStringLiteral("credentialBundle"))) return false;
    // Validate ownership/encoding only. Until a service verification endpoint
    // is implemented, transport and permissions remain strictly anonymous.
    return KugouCredentialRecord::decode(secrets->value(QStringLiteral("credentialBundle")),
        m_configuration.sourceInstanceId, m_configuration.accountId).has_value();
}
CapabilitySetV2 KugouSourceSession::capabilities() const
{
    CapabilitySetV2 result;
    for (auto action : {SourceActionV2::Artwork, SourceActionV2::Lyrics, SourceActionV2::Download,
                        SourceActionV2::Favorite, SourceActionV2::Unfavorite, SourceActionV2::Rating,
                        SourceActionV2::Scrobble, SourceActionV2::CreatePlaylist, SourceActionV2::UpdatePlaylist,
                        SourceActionV2::DeletePlaylist, SourceActionV2::AddPlaylistTracks, SourceActionV2::RemovePlaylistTracks,
                        SourceActionV2::FetchPlayQueue, SourceActionV2::SavePlayQueue, SourceActionV2::FetchBookmarks,
                        SourceActionV2::CreateBookmark, SourceActionV2::DeleteBookmark})
        result.serverActions.insert(action, {AvailabilityV2::Unsupported, "source.operation.unsupported", {}});
    result.serverActions.insert(SourceActionV2::Play, m_state == SourceSessionStateV2::Ready
        ? ActionAvailabilityV2{AvailabilityV2::Available, {}, {}}
        : ActionAvailabilityV2{AvailabilityV2::Unavailable, "kugou.session.notReady", {}});
    result.accountActions = result.serverActions;
    return result;
}
QUuid KugouSourceSession::enqueue(Request request)
{
    const auto id = QUuid::createUuid();
    m_requests.insert(id, std::move(request));
    const QPointer<KugouSourceSession> guard(this);
    emit requestStarted(id);
    if (guard && m_requests.contains(id))
        QTimer::singleShot(0, this, [this, id] { dispatch(id); });
    return id;
}
QUuid KugouSourceSession::open() { return enqueue({Kind::Open}); }
QUuid KugouSourceSession::fetchPage(const PageQueryV2 &query) { return enqueue({Kind::Page, query}); }
QUuid KugouSourceSession::resolveStream(const MediaRefV2 &media) { return enqueue({Kind::Stream, {}, media}); }
std::optional<MediaItemV2> KugouSourceSession::lookupItem(const MediaRefV2 &ref) const
{
    if (m_state != SourceSessionStateV2::Ready) return std::nullopt;
    const auto it = m_tracks.constFind(ref.entityId);
    if (it == m_tracks.cend() || it->ref != ref) return std::nullopt;
    // Only the current session's validated search DTOs are indexed. No I/O,
    // historical snapshot import, or stream URL is involved in this lookup.
    return *it;
}
QUuid KugouSourceSession::fetchArtwork(const MediaRefV2 &media) { return enqueue({Kind::Unsupported, {}, media}); }
QUuid KugouSourceSession::fetchLyrics(const MediaRefV2 &media) { return enqueue({Kind::Unsupported, {}, media}); }

void KugouSourceSession::dispatch(const QUuid &id)
{
    if (!m_requests.contains(id)) return;
    const auto request = m_requests.value(id);
    if (request.kind == Kind::Open) {
        if (m_state != SourceSessionStateV2::Closed) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "kugou.session.alreadyOpen")); return;
        }
        m_openId = id;
        m_state = SourceSessionStateV2::Connecting;
        const QPointer<KugouSourceSession> guard(this);
        emit stateChanged(m_state);
        if (!guard || !m_requests.contains(id)) return;
        if (!validConfiguration()) {
            m_state = SourceSessionStateV2::Failed;
            emit stateChanged(m_state);
            if (guard) fail(id, failure(SourceErrorKindV2::InvalidRequest, "kugou.config.invalid"));
            return;
        }
        m_state = SourceSessionStateV2::Ready;
        emit capabilitiesChanged(capabilities());
        if (!guard || !m_requests.contains(id)) return;
        emit stateChanged(m_state);
        if (guard && m_requests.contains(id)) { m_requests.remove(id); m_openId = {}; }
        return;
    }
    if (m_state != SourceSessionStateV2::Ready) {
        fail(id, failure(SourceErrorKindV2::Unavailable, "kugou.session.notReady")); return;
    }
    QUrlQuery parameters;
    auto endpoint = KugouApiClient::Endpoint::SearchTracks;
    if (request.kind == Kind::Unsupported) {
        fail(id, failure(SourceErrorKindV2::Unsupported, "source.operation.unsupported")); return;
    }
    if (request.kind == Kind::Page) {
        const auto query = request.query;
        if (query.scope.sourceInstanceId != m_configuration.sourceInstanceId
            || query.limit < 1 || query.limit > 100) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "kugou.page.invalid")); return;
        }
        if (query.page != MusicPageKindV2::Search || query.section != PageSectionKindV2::Tracks) {
            fail(id, failure(SourceErrorKindV2::Unsupported, "source.operation.unsupported")); return;
        }
        if (query.searchText.trimmed().isEmpty() || query.searchText.size() > 512 || !query.filters.isEmpty()) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "kugou.page.invalid")); return;
        }
        int page = 1;
        if (!query.cursor.isEmpty()) {
            const auto it = m_cursors.constFind(query.cursor);
            if (it == m_cursors.cend() || it->search != query.searchText.trimmed() || it->limit != query.limit) {
                fail(id, failure(SourceErrorKindV2::InvalidRequest, "kugou.cursor.invalid")); return;
            }
            page = it->page;
        }
        m_requests[id].page = page;
        parameters.addQueryItem("keyword", query.searchText.trimmed());
        parameters.addQueryItem("page", QString::number(page));
        parameters.addQueryItem("pagesize", QString::number(query.limit));
        parameters.addQueryItem("format", "json");
    } else {
        const auto media = request.media;
        const auto hash = KugouMappers::canonicalHash(media.entityId);
        if (media.sourcePluginId != m_configuration.sourceId
            || media.sourceInstanceId != m_configuration.sourceInstanceId
            || media.accountId != m_configuration.accountId || media.entityType != MediaEntityTypeV2::Track
            || hash.isEmpty() || hash != media.entityId) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "source.media.invalid")); return;
        }
        const auto indexed = lookupItem(media);
        if ((indexed && indexed->availableActions.value(SourceActionV2::Play).state != AvailabilityV2::Available)
            || (!indexed && m_tracks.size() >= 4096)) {
            fail(id, failure(SourceErrorKindV2::Authorization, "kugou.track.unverified")); return;
        }
        m_requests[id].needsVerification = !indexed;
        endpoint = KugouApiClient::Endpoint::ResolveTrack;
        parameters.addQueryItem("cmd", "playInfo"); parameters.addQueryItem("hash", hash);
    }
    const auto clientId = m_client->get(endpoint, parameters);
    m_requests[id].clientId = clientId; m_clientIds.insert(clientId, id);
}
void KugouSourceSession::succeeded(const QUuid &clientId, const QJsonObject &response)
{
    const auto id = m_clientIds.take(clientId);
    if (id.isNull() || !m_requests.contains(id)) return;
    const auto request = m_requests.value(id);
    if (request.kind == Kind::Stream) {
        std::optional<MediaItemV2> verified;
        if (request.needsVerification) {
            verified = KugouMappers::verifiedResourceTrack(response, request.media);
            const auto current = lookupItem(request.media);
            if (!verified || (current && current->availableActions.value(SourceActionV2::Play).state
                != AvailabilityV2::Available) || (!current && m_tracks.size() >= 4096)) {
                fail(id, failure(SourceErrorKindV2::Authorization, "kugou.track.unverified")); return;
            }
            if (verified->availableActions.value(SourceActionV2::Play).state != AvailabilityV2::Available) {
                m_tracks.insert(request.media.entityId, *verified);
                fail(id, failure(SourceErrorKindV2::Authorization, "kugou.track.unverified")); return;
            }
        }
        const auto stream = KugouMappers::stream(response, request.media);
        if (!stream) { fail(id, failure(SourceErrorKindV2::Unavailable, "kugou.stream.invalid")); return; }
        if (verified) m_tracks.insert(request.media.entityId, *verified);
        const auto indexed = lookupItem(request.media);
        if (!indexed || indexed->availableActions.value(SourceActionV2::Play).state != AvailabilityV2::Available) {
            fail(id, failure(SourceErrorKindV2::Authorization, "kugou.track.unverified")); return;
        }
        m_requests.remove(id); emit streamReady(id, *stream); return;
    }
    const auto data = response.value("data").toObject();
    const auto total = data.value("total"); const double count = total.toDouble(-1);
    const auto rows = data.value("info").toArray();
    if (response.value("status").toDouble(-1) != 1 || !data.value("info").isArray()
        || !total.isDouble() || !std::isfinite(count) || count < 0 || count > INT_MAX
        || std::floor(count) != count || rows.size() > request.query.limit) {
        fail(id, failure(SourceErrorKindV2::Unavailable, "kugou.page.invalidResponse")); return;
    }
    PageSectionV2 section;
    section.sectionId = "tracks"; section.kind = PageSectionKindV2::Tracks;
    section.titleKey = "source.search.tracks"; section.layoutHint = "list";
    QSet<QString> hashes;
    for (const auto &row : rows) {
        auto item = KugouMappers::track(row.toObject(), identity());
        if (!item) continue;
        auto right = item->availableActions.value(SourceActionV2::Play);
        // A late older free response must not undo a restriction observed in
        // this anonymous session. Reauthentication/reopen will revalidate.
        if (m_tracks.contains(item->ref.entityId))
            right = intersectActionAvailabilityV2({m_tracks.value(item->ref.entityId)
                .availableActions.value(SourceActionV2::Play), right});
        item->availableActions.insert(SourceActionV2::Play, right);
        if (m_tracks.size() >= 4096 && !m_tracks.contains(item->ref.entityId))
            right = {AvailabilityV2::Unavailable, "kugou.track.unverified", {}};
        else
            m_tracks.insert(item->ref.entityId, *item);
        if (hashes.contains(item->ref.entityId)) {
            for (auto &existing : section.items)
                if (existing.ref.entityId == item->ref.entityId)
                    existing.availableActions.insert(SourceActionV2::Play, right);
            continue;
        }
        hashes.insert(item->ref.entityId);
        item->availableActions.insert(SourceActionV2::Play, right);
        section.items.append(*item);
    }
    const qint64 offset = qint64(request.page) * request.query.limit;
    section.hasMore = !rows.isEmpty() && rows.size() == request.query.limit && offset < qint64(count);
    if (section.hasMore) {
        if (m_cursors.size() >= 1024) m_cursors.clear();
        section.nextCursor = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_cursors.insert(section.nextCursor, {request.query.searchText.trimmed(), request.query.limit, request.page + 1});
    }
    PageResultV2 result;
    result.sections.append(section);
    result.sourceStates.insert(m_configuration.sourceInstanceId,
        {section.items.isEmpty() ? SourcePageLoadStateV2::Empty : SourcePageLoadStateV2::Ready, {}});
    m_requests.remove(id); emit pageReady(id, result);
}
void KugouSourceSession::fail(const QUuid &id, const SourceErrorV2 &error)
{
    if (!m_requests.contains(id)) return;
    const auto clientId = m_requests.take(id).clientId;
    m_clientIds.remove(clientId); m_client->cancel(clientId);
    if (m_openId == id) m_openId = {};
    emit requestFailed(id, error);
}
void KugouSourceSession::cancel(const QUuid &id)
{
    if (!m_requests.contains(id)) return;
    const auto clientId = m_requests.take(id).clientId;
    m_clientIds.remove(clientId); m_client->cancel(clientId);
    if (m_openId == id) {
        m_openId = {}; m_state = SourceSessionStateV2::Closed;
        const QPointer<KugouSourceSession> guard(this);
        emit capabilitiesChanged(capabilities());
        if (guard) emit stateChanged(m_state);
    }
}
void KugouSourceSession::clearRequests()
{
    m_requests.clear(); m_clientIds.clear(); m_openId = {};
    m_client->cancelAll(); m_cursors.clear(); m_tracks.clear();
}
void KugouSourceSession::close()
{
    if (m_closing) return;
    clearRequests();
    if (m_state == SourceSessionStateV2::Closed) return;
    m_closing = true; m_state = SourceSessionStateV2::Closing;
    const QPointer<KugouSourceSession> guard(this);
    emit stateChanged(m_state);
    if (!guard) return;
    m_state = SourceSessionStateV2::Closed;
    emit capabilitiesChanged(capabilities());
    if (!guard) return;
    m_closing = false; emit stateChanged(m_state);
}
