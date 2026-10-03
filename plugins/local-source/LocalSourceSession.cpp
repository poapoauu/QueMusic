#include "LocalSourceSession.h"
#include "core/local-media/LocalMediaFiles.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <utility>

namespace {
SourceErrorV2 failure(SourceErrorKindV2 kind, const QString &key)
{
    return {kind, key, {}, {}, false};
}
const ActionAvailabilityV2 available{AvailabilityV2::Available, {}, {}};
QString fileId(const QString &path)
{
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}
}

LocalSourceSession::LocalSourceSession(SourceConfigurationV2 configuration,
                                     LocalLibraryIndexPool *pool, QObject *parent)
    : IMusicSourceSessionV2(parent), m_configuration(std::move(configuration)),
      m_pool(pool), m_events(new SourceContentEventsV1(this))
{
    m_resources.setMaxThreadCount(1);
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << m_configuration.pluginPackageId << m_configuration.sourceId
           << m_configuration.sourceInstanceId << m_configuration.accountId
           << m_configuration.displayName << m_configuration.parameters << m_configuration.secret;
    m_configToken = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

LocalSourceSession::~LocalSourceSession()
{
    close();
    m_resources.waitForDone();
}
SourceIdentityV2 LocalSourceSession::identity() const
{
    return {m_configuration.sourceId, m_configuration.sourceInstanceId,
            m_configuration.accountId, m_configuration.displayName};
}
SourceSessionStateV2 LocalSourceSession::state() const { return m_state; }
CapabilitySetV2 LocalSourceSession::capabilities() const
{
    CapabilitySetV2 result;
    if (m_state == SourceSessionStateV2::Ready)
        for (const auto action : {SourceActionV2::Play, SourceActionV2::Artwork,
                                  SourceActionV2::Lyrics}) {
            result.serverActions.insert(action, available);
            result.accountActions.insert(action, available);
        }
    return result;
}
QUuid LocalSourceSession::start(Request request)
{
    const QUuid id = QUuid::createUuid();
    m_requests.insert(id, std::move(request));
    emit requestStarted(id);
    return id;
}
void LocalSourceSession::fail(const QUuid &id, SourceErrorV2 error)
{
    if (m_requests.remove(id)) emit requestFailed(id, std::move(error));
}
QUuid LocalSourceSession::open()
{
    const QUuid id = start({RequestKind::Open});
    if (!m_requests.contains(id)) return id;
    if (m_state != SourceSessionStateV2::Closed) {
        QTimer::singleShot(0, this, [this, id] {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.session.alreadyOpen"));
        });
        return id;
    }
    m_state = SourceSessionStateV2::Connecting;
    emit stateChanged(m_state);
    SourceErrorV2 configError;
    const auto scan = LocalSourceScanner::parseConfig(m_configuration.parameters, &configError);
    const auto identityValid = m_configuration.pluginPackageId == QLatin1String("org.quemusic.source.local")
        && m_configuration.sourceId == QLatin1String("local")
        && !m_configuration.accountId.isEmpty()
        && m_configuration.sourceInstanceId == QLatin1String("local/") + m_configuration.accountId
        && m_configuration.secret.isEmpty();
    const QVariant scanOnOpen = m_configuration.parameters.value("scanOnOpen", true);
    const QVariant watch = m_configuration.parameters.value("watchChanges", false);
    if (!scan || !identityValid || scanOnOpen.metaType().id() != QMetaType::Bool
        || watch.metaType().id() != QMetaType::Bool
        || !QFileInfo(scan->canonicalRoot).isReadable()) {
        const SourceErrorV2 error = !scan
            ? (configError.kind == SourceErrorKindV2::NotFound
                   ? failure(SourceErrorKindV2::Unavailable, "local.root.unavailable")
                   : configError)
            : !identityValid || scanOnOpen.metaType().id() != QMetaType::Bool
              || watch.metaType().id() != QMetaType::Bool
                ? failure(SourceErrorKindV2::InvalidRequest, "local.config.invalid")
                : failure(SourceErrorKindV2::Unavailable, "local.root.unavailable");
        QTimer::singleShot(0, this, [this, id, error] {
            if (!m_requests.contains(id)) return;
            m_state = SourceSessionStateV2::Failed;
            emit stateChanged(m_state);
            fail(id, error);
        });
        return id;
    }
    m_index = m_pool->acquire(m_configuration, *scan);
    connect(m_index.get(), &LocalLibraryIndex::snapshotChanged,
            this, &LocalSourceSession::onSnapshotChanged);
    connect(m_index.get(), &LocalLibraryIndex::refreshFailed,
            this, &LocalSourceSession::onRefreshFailed);
    if (scanOnOpen.toBool() && !m_index->snapshot()) m_index->requestScan();
    else QTimer::singleShot(0, this, [this, id] {
        if (!m_requests.remove(id)) return;
        m_state = SourceSessionStateV2::Ready;
        emit stateChanged(m_state);
        emit capabilitiesChanged(capabilities());
    });
    return id;
}
void LocalSourceSession::close()
{
    if (m_state == SourceSessionStateV2::Closed && !m_index) return;
    m_requests.clear();
    m_state = SourceSessionStateV2::Closing;
    emit stateChanged(m_state);
    if (m_index) disconnect(m_index.get(), nullptr, this, nullptr);
    m_index.reset();
    m_state = SourceSessionStateV2::Closed;
    emit capabilitiesChanged(capabilities());
    emit stateChanged(m_state);
}
void LocalSourceSession::cancel(const QUuid &id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end()) return;
    const bool opening = it->kind == RequestKind::Open;
    m_requests.erase(it);
    if (opening) close();
}
void LocalSourceSession::onSnapshotChanged(quint64 revision)
{
    emit m_events->contentChanged(revision);
    for (const auto &id : m_requests.keys()) {
        const auto request = m_requests.value(id);
        if (request.kind == RequestKind::Open) {
            m_requests.remove(id);
            m_state = SourceSessionStateV2::Ready;
            emit stateChanged(m_state);
            emit capabilitiesChanged(capabilities());
        } else if (request.kind == RequestKind::Page) finishPage(id);
        else if (request.kind == RequestKind::Rescan && revision > request.previousRevision) {
            m_requests.remove(id);
            emit settingsActionCompleted(id, QStringLiteral("rescan"));
        }
    }
}
void LocalSourceSession::onRefreshFailed(SourceErrorV2 error)
{
    emit m_events->refreshFailed(error);
    for (const auto &id : m_requests.keys()) {
        const auto kind = m_requests.value(id).kind;
        if (kind == RequestKind::Open) {
            m_state = SourceSessionStateV2::Failed;
            emit stateChanged(m_state);
            fail(id, error);
        } else if (kind == RequestKind::Page || kind == RequestKind::Rescan) fail(id, error);
    }
}
bool LocalSourceSession::validMedia(const MediaRefV2 &media) const
{
    return media.sourcePluginId == QLatin1String("local")
        && media.sourceInstanceId == m_configuration.sourceInstanceId
        && media.accountId == m_configuration.accountId;
}
QByteArray LocalSourceSession::queryKey(const PageQueryV2 &query) const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << int(query.page) << int(query.section) << query.scope.sourceInstanceId
           << query.searchText << query.filters << query.limit;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QUuid LocalSourceSession::fetchPage(const PageQueryV2 &query)
{
    const QUuid id = start({RequestKind::Page, query});
    QTimer::singleShot(0, this, [this, id] {
        if (!m_requests.contains(id)) return;
        if (m_state != SourceSessionStateV2::Ready || !m_index) {
            fail(id, failure(SourceErrorKindV2::Unavailable, "local.session.unavailable"));
            return;
        }
        const auto query = m_requests.value(id).query;
        if (query.limit < 1 || query.limit > 200
            || query.scope.sourceInstanceId != m_configuration.sourceInstanceId
            || (query.page != MusicPageKindV2::Category && query.page != MusicPageKindV2::Search)) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.page.invalid"));
            return;
        }
        if (!m_index->snapshot()) m_index->requestScan();
        else finishPage(id);
    });
    return id;
}

void LocalSourceSession::finishPage(const QUuid &id)
{
    if (!m_requests.contains(id) || !m_index) return;
    const auto snapshot = m_index->snapshot();
    if (!snapshot) return;
    LocalIdentitySnapshot persisted;
    if (!m_index->persistedIdentities(&persisted)) {
        fail(id, failure(SourceErrorKindV2::Unavailable, "local.identity.readFailed"));
        return;
    }
    const PageQueryV2 query = m_requests.value(id).query;
    const QString root = snapshot->scan.watchedDirectories.value(0);
    if (root.isEmpty()) {
        fail(id, failure(SourceErrorKindV2::Unavailable, "local.root.unavailable"));
        return;
    }
    const bool rootCard = query.page == MusicPageKindV2::Category
        && query.filters.value("entityType") == int(MediaEntityTypeV2::Directory)
        && query.filters.size() == 1;
    const QString directoryId = query.filters.value("directoryId").toString();
    if ((query.page == MusicPageKindV2::Search && !query.filters.isEmpty())
        || (query.page == MusicPageKindV2::Category && !rootCard
            && (query.filters.size() != 1 || directoryId.isEmpty()))) {
        fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.page.invalid"));
        return;
    }
    PageSectionV2 section;
    section.sectionId = QStringLiteral("local.library");
    section.titleKey = QStringLiteral("local.library");
    section.kind = query.page == MusicPageKindV2::Search
        ? PageSectionKindV2::SearchResults : PageSectionKindV2::Tracks;
    section.layoutHint = query.page == MusicPageKindV2::Category
        ? QStringLiteral("directories") : QString();
    QList<MediaItemV2> items;
    const auto directoryItem = [this, &snapshot](const QString &path) {
        MediaItemV2 item;
        item.ref = {QStringLiteral("local"), m_configuration.sourceInstanceId,
                    m_configuration.accountId, MediaEntityTypeV2::Directory,
                    snapshot->directoryIdByPath.value(path)};
        item.title = QFileInfo(path).fileName();
        return item;
    };
    if (rootCard) {
        if (persisted.directoryIdsByPath.value(root) != snapshot->directoryIdByPath.value(root)) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.reference.invalid"));
            return;
        }
        items.append(directoryItem(root));
    }
    else if (query.page == MusicPageKindV2::Category) {
        const QString directoryPath = snapshot->directoryPathById.value(directoryId);
        const auto directory = directoryPath.isEmpty() ? std::optional<QString>()
            : LocalSourceScanner::validatedPath({root, true, {}}, fileId(directoryPath), true);
        if (!directory || *directory != directoryPath
            || persisted.directoryIdsByPath.value(directoryPath) != directoryId
            || !snapshot->scan.watchedDirectories.contains(*directory)) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.reference.invalid"));
            return;
        }
        for (const auto &path : snapshot->scan.watchedDirectories)
            if (path != *directory && QFileInfo(path).absolutePath() == *directory)
                if (persisted.directoryIdsByPath.value(path) == snapshot->directoryIdByPath.value(path))
                    items.append(directoryItem(path));
        for (const auto &entry : snapshot->scan.entries) {
            if (QFileInfo(entry.canonicalPath).absolutePath() != *directory) continue;
            if (persisted.trackIdsByPath.value(entry.canonicalPath)
                != snapshot->trackIdByPath.value(entry.canonicalPath)) continue;
            auto item = entry.item;
            item.ref = {QStringLiteral("local"), m_configuration.sourceInstanceId,
                        m_configuration.accountId, MediaEntityTypeV2::Track,
                        snapshot->trackIdByPath.value(entry.canonicalPath)};
            for (const auto action : {SourceActionV2::Play, SourceActionV2::Artwork,
                                      SourceActionV2::Lyrics})
                item.availableActions.insert(action, available);
            items.append(item);
        }
    } else {
        for (const auto &entry : snapshot->scan.entries) {
            if (persisted.trackIdsByPath.value(entry.canonicalPath)
                != snapshot->trackIdByPath.value(entry.canonicalPath)) continue;
            const QString text = query.searchText;
            if (!entry.item.title.contains(text, Qt::CaseInsensitive)
                && !entry.item.album.contains(text, Qt::CaseInsensitive)
                && !entry.item.artists.join(' ').contains(text, Qt::CaseInsensitive)
                && !QFileInfo(entry.canonicalPath).fileName().contains(text, Qt::CaseInsensitive))
                continue;
            auto item = entry.item;
            item.ref = {QStringLiteral("local"), m_configuration.sourceInstanceId,
                        m_configuration.accountId, MediaEntityTypeV2::Track,
                        snapshot->trackIdByPath.value(entry.canonicalPath)};
            item.availableActions.insert(SourceActionV2::Play, available);
            items.append(item);
        }
    }
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
        if (a.ref.entityType != b.ref.entityType)
            return a.ref.entityType == MediaEntityTypeV2::Directory;
        return a.ref.entityId < b.ref.entityId;
    });
    int offset = 0;
    if (!query.cursor.isEmpty()) {
        const auto raw = QByteArray::fromBase64(query.cursor.toLatin1(), QByteArray::Base64UrlEncoding);
        const auto object = QJsonDocument::fromJson(raw).object();
        if (object.value("config").toString().toLatin1() != m_configToken
            || object.value("query").toString().toLatin1() != queryKey(query)
            || object.value("revision").toVariant().toULongLong() != snapshot->revision) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.cursor.stale"));
            return;
        }
        offset = object.value("offset").toInt(-1);
        if (offset < 1 || offset > items.size()) {
            fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.cursor.stale"));
            return;
        }
    }
    section.items = items.mid(offset, query.limit);
    section.hasMore = offset + section.items.size() < items.size();
    if (section.hasMore) {
        QJsonObject cursor{{"config", QString::fromLatin1(m_configToken)},
                           {"query", QString::fromLatin1(queryKey(query))},
                           {"revision", qint64(snapshot->revision)},
                           {"offset", offset + section.items.size()}};
        section.nextCursor = QString::fromLatin1(QJsonDocument(cursor).toJson(QJsonDocument::Compact)
                                                  .toBase64(QByteArray::Base64UrlEncoding
                                                            | QByteArray::OmitTrailingEquals));
    }
    PageResultV2 result;
    result.sections.append(section);
    result.sourceStates.insert(m_configuration.sourceInstanceId,
                               {items.isEmpty() ? SourcePageLoadStateV2::Empty
                                                : SourcePageLoadStateV2::Ready, {}});
    if (m_requests.remove(id)) emit pageReady(id, result);
}

QUuid LocalSourceSession::resolveStream(const MediaRefV2 &media)
{
    const QUuid id = start({RequestKind::Stream, {}, media});
    QTimer::singleShot(0, this, [this, id] { finishResource(id); });
    return id;
}
std::optional<MediaRefV2> LocalSourceSession::claimLegacyFile(const QUrl &fileUrl) const
{
    if (m_state != SourceSessionStateV2::Ready || !m_index || !fileUrl.isValid()
        || !fileUrl.isLocalFile() || !fileUrl.authority().isEmpty()
        || fileUrl.hasQuery() || fileUrl.hasFragment()) return std::nullopt;
    const auto snapshot = m_index->snapshot();
    if (!snapshot) return std::nullopt;
    const QString root = snapshot->scan.watchedDirectories.value(0);
    const auto canonical = LocalMediaFiles::boundedPath(fileUrl.toLocalFile(), root);
    if (!canonical) return std::nullopt;
    const auto path = LocalSourceScanner::validatedPath({root, true, {}},
                                                         fileId(*canonical), false);
    if (!path) return std::nullopt;
    const QString trackId = snapshot->trackIdByPath.value(*path);
    if (trackId.isEmpty()) return std::nullopt;
    LocalIdentitySnapshot persisted;
    if (!m_index->persistedIdentities(&persisted)
        || persisted.trackIdsByPath.value(*path) != trackId) return std::nullopt;
    return MediaRefV2{m_configuration.sourceId, m_configuration.sourceInstanceId,
                      m_configuration.accountId, MediaEntityTypeV2::Track, trackId};
}
std::optional<MediaItemV2> LocalSourceSession::lookupItem(const MediaRefV2 &ref) const
{
    if (m_state != SourceSessionStateV2::Ready || !m_index || !validMedia(ref)
        || ref.entityType != MediaEntityTypeV2::Track) return std::nullopt;
    const auto snapshot = m_index->snapshot();
    if (!snapshot) return std::nullopt;
    const QString canonicalPath = snapshot->trackPathById.value(ref.entityId);
    if (canonicalPath.isEmpty()) return std::nullopt;
    LocalIdentitySnapshot persisted;
    if (!m_index->persistedIdentities(&persisted)
        || persisted.trackIdsByPath.value(canonicalPath) != ref.entityId)
        return std::nullopt;
    const QString root = snapshot->scan.watchedDirectories.value(0);
    const auto path = LocalSourceScanner::validatedPath({root, true, {}},
                                                         fileId(canonicalPath), false);
    if (!path || *path != canonicalPath) return std::nullopt;
    for (const auto &entry : snapshot->scan.entries) {
        if (entry.canonicalPath != canonicalPath) continue;
        MediaItemV2 item = entry.item;
        item.ref = ref;
        for (const auto action : {SourceActionV2::Play, SourceActionV2::Artwork,
                                  SourceActionV2::Lyrics})
            item.availableActions.insert(action, available);
        return item;
    }
    return std::nullopt;
}
QUuid LocalSourceSession::fetchArtwork(const MediaRefV2 &media)
{
    const QUuid id = start({RequestKind::Artwork, {}, media});
    QTimer::singleShot(0, this, [this, id] { finishResource(id); });
    return id;
}
QUuid LocalSourceSession::fetchLyrics(const MediaRefV2 &media)
{
    const QUuid id = start({RequestKind::Lyrics, {}, media});
    QTimer::singleShot(0, this, [this, id] { finishResource(id); });
    return id;
}

void LocalSourceSession::finishResource(const QUuid &id)
{
    if (!m_requests.contains(id)) return;
    const auto request = m_requests.value(id);
    if (m_state != SourceSessionStateV2::Ready || !m_index) {
        fail(id, failure(SourceErrorKindV2::Unavailable, "local.session.unavailable"));
        return;
    }
    if (!validMedia(request.media) || request.media.entityType != MediaEntityTypeV2::Track) {
        fail(id, failure(SourceErrorKindV2::InvalidRequest, "local.reference.invalid"));
        return;
    }
    const auto snapshot = m_index->snapshot();
    if (!snapshot) {
        fail(id, failure(SourceErrorKindV2::NotFound, "local.reference.invalid"));
        return;
    }
    const QString canonicalPath = snapshot->trackPathById.value(request.media.entityId);
    if (canonicalPath.isEmpty()) {
        fail(id, failure(SourceErrorKindV2::NotFound, "local.reference.invalid"));
        return;
    }
    LocalIdentitySnapshot persisted;
    if (!m_index->persistedIdentities(&persisted)
        || persisted.trackIdsByPath.value(canonicalPath) != request.media.entityId) {
        fail(id, failure(SourceErrorKindV2::NotFound, "local.reference.invalid"));
        return;
    }
    const QString root = snapshot->scan.watchedDirectories.value(0);
    const auto path = LocalSourceScanner::validatedPath({root, true, {}},
                                                         fileId(canonicalPath), false);
    if (!path || *path != canonicalPath) {
        fail(id, failure(SourceErrorKindV2::NotFound, "local.reference.invalid"));
        return;
    }
    if (request.kind == RequestKind::Stream) {
        StreamDescriptorV2 stream;
        stream.media = request.media;
        stream.url = QUrl::fromLocalFile(*path);
        stream.seekable = true;
        if (m_requests.remove(id)) emit streamReady(id, stream);
        return;
    }
    const auto kind = request.kind;
    const auto media = request.media;
    QPointer<LocalSourceSession> guard(this);
    m_resources.start(QRunnable::create([guard, id, kind, media, file = *path, root] {
        QVariantMap payload = kind == RequestKind::Artwork
            ? LocalMediaFiles::artworkPayload(file, root)
            : QVariantMap{{QStringLiteral("lyrics"), LocalMediaFiles::lyricsText(file, root)}};
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, id, kind, media, payload = std::move(payload)] {
            if (!guard || !guard->m_requests.remove(id)) return;
            if (kind == RequestKind::Artwork && !payload.contains("bytes"))
                emit guard->requestFailed(id,
                    failure(SourceErrorKindV2::NotFound, "local.artwork.notFound"));
            else emit guard->actionCompleted(id,
                {kind == RequestKind::Artwork ? SourceActionV2::Artwork : SourceActionV2::Lyrics,
                 media, payload});
        }, Qt::QueuedConnection);
    }));
}

SettingsActionCapabilitiesV2 LocalSourceSession::settingsCapabilities() const
{
    SettingsActionCapabilitiesV2 result;
    if (m_state == SourceSessionStateV2::Ready) {
        result.serverActions.insert("rescan", available);
        result.accountActions.insert("rescan", available);
    }
    return result;
}
QUuid LocalSourceSession::runSettingsAction(const QString &actionId)
{
    Request request{RequestKind::Rescan};
    request.previousRevision = m_index && m_index->snapshot() ? m_index->snapshot()->revision : 0;
    const QUuid id = start(std::move(request));
    QTimer::singleShot(0, this, [this, id, actionId] {
        if (!m_requests.contains(id)) return;
        if (actionId != QLatin1String("rescan"))
            fail(id, failure(SourceErrorKindV2::Unsupported, "local.action.unsupported"));
        else if (m_state != SourceSessionStateV2::Ready || !m_index)
            fail(id, failure(SourceErrorKindV2::Unavailable, "local.session.unavailable"));
        else m_index->requestScan();
    });
    return id;
}
SourceContentEventsV1 *LocalSourceSession::contentEvents() const { return m_events; }
