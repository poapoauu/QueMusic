#include "MediaActionRouter.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QDir>
#include <QSet>
#include <QTimer>
#include <cmath>
#include <limits>

namespace {
struct Item {
    MediaRefV2 ref;
    QHash<SourceActionV2, ActionAvailabilityV2> actions;
};
QString owned(const QString &value) { return QString(value.constData(), value.size()); }
bool integer(const QVariant &value, qint64 minimum, qint64 maximum, qint64 *out = nullptr)
{
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Double: break;
    default: return false;
    }
    // JSON-backed model maps may contain integral doubles. Strings/bools never coerce.
    const long double n = value.metaType().id() == QMetaType::ULongLong
        ? static_cast<long double>(value.toULongLong())
        : value.metaType().id() == QMetaType::Double ? static_cast<long double>(value.toDouble())
                                                   : static_cast<long double>(value.toLongLong());
    if (!std::isfinite(n) || std::floor(n) != n || n < minimum || n > maximum) return false;
    if (out) *out = static_cast<qint64>(n);
    return true;
}
bool text(const QVariant &value, bool nonempty = true)
{
    return value.metaType().id() == QMetaType::QString
        && (!nonempty || !value.toString().trimmed().isEmpty());
}
bool parseItem(const QVariantMap &map, Item *item)
{
    if (map.value("ref").metaType().id() != QMetaType::QVariantMap
        || map.value("availableActions").metaType().id() != QMetaType::QVariantMap) return false;
    const auto ref = map.value("ref").toMap();
    for (auto key : {"sourcePluginId", "sourceInstanceId", "accountId", "entityId"})
        if (!text(ref.value(key))) return false;
    if (!integer(ref.value("entityType"), 0, int(MediaEntityTypeV2::Directory))) return false;
    item->ref = mediaRefV2FromVariantMap(ref);
    const auto actions = map.value("availableActions").toMap();
    for (auto it = actions.cbegin(); it != actions.cend(); ++it) {
        bool ok = false; const int key = it.key().toInt(&ok);
        if (!ok || key < 0 || key > int(SourceActionV2::DeleteBookmark)
            || QString::number(key) != it.key() || it->metaType().id() != QMetaType::QVariantMap) return false;
        const auto entry = it->toMap();
        if (!integer(entry.value("state"), 0, int(AvailabilityV2::Forbidden))
            || (entry.contains("reasonKey") && !text(entry.value("reasonKey"), false))
            || entry.value("constraints").metaType().id() != QMetaType::QVariantMap) return false;
        // No arbitrary QVariant escapes this boundary; the shared helper validates
        // restriction types and retains only understood value restrictions.
        item->actions[SourceActionV2(key)] = intersectActionAvailabilityV2({
            {AvailabilityV2(entry.value("state").toInt()), {}, entry.value("constraints").toMap()}});
    }
    return true;
}
bool sameOwner(const MediaRefV2 &a, const MediaRefV2 &b)
{
    return a.sourcePluginId == b.sourcePluginId && a.sourceInstanceId == b.sourceInstanceId
        && a.accountId == b.accountId;
}
bool localDestination(const QUrl &url)
{
    return url.isValid() && url.isLocalFile() && url.host().isEmpty() && url.userInfo().isEmpty()
        && !url.hasQuery() && !url.hasFragment() && QDir::isAbsolutePath(url.toLocalFile());
}
QVariantMap errorMap(AvailabilityV2 state, const QString &key, SourceErrorKindV2 kind)
{
    return {{"state", int(state)}, {"kind", int(kind)}, {"messageKey", key}, {"retryable", false}};
}
QVariantMap invalidError()
{
    return errorMap(AvailabilityV2::Unavailable, QStringLiteral("music.actionInvalidRequest"), SourceErrorKindV2::InvalidRequest);
}
QVariantMap capabilityError(AvailabilityV2 state)
{
    const auto kind = state == AvailabilityV2::Forbidden ? SourceErrorKindV2::Authorization
        : state == AvailabilityV2::Unsupported ? SourceErrorKindV2::Unsupported : SourceErrorKindV2::Unavailable;
    return errorMap(state, QStringLiteral("music.actionNotAvailable"), kind);
}
void disconnectAll(QList<QMetaObject::Connection> &connections)
{
    for (const auto &connection : connections) QObject::disconnect(connection);
    connections.clear();
}
}

struct MediaActionRouter::Request {
    struct Terminal { QVariantMap result; QVariantMap error; };
    QUuid hostId = QUuid::createUuid();
    SourceActionV2 action = SourceActionV2::Favorite;
    Item item;
    QList<Item> additions;
    PlaylistChangeV2 change;
    bool favorite = false;
    int rating = 0;
    qint64 positionMs = 0;
    QString name;
    QUrl destination;
    QPointer<SourceRegistry> registry;
    QPointer<PluginManager> plugins;
    QPointer<QObject> root;
    QPointer<IMusicSourceSessionV2> session;
    QString package;
    PluginLease lease;
    QUuid providerId;
    QSet<QUuid> started;
    QHash<QUuid, Terminal> inlineTerminals;
    QList<QMetaObject::Connection> connections;
    Terminal terminal;
    bool invoking = false;
    bool returned = false;
    bool settled = false;
    bool lifecycleEnded = false;
    bool invalidated = false;
    bool abandoned = false;
    bool queued = false;
    bool providerFinished = false;

    bool current() const
    {
        return !abandoned && !lifecycleEnded && !invalidated && registry && plugins && root && session
            && session->parent() == registry && plugins->pluginInstance(package) == root
            && plugins->plugin(package).state == PluginState::Loaded;
    }
    void reject(QVariantMap error)
    {
        terminal = {{}, std::move(error)};
        settled = true;
    }
    void cancel()
    {
        // The registry owns close/disable cancellation. Never invoke cancel on
        // a closing borrowed session, or from inside its own provider callback.
        if (invoking || providerFinished || lifecycleEnded || !session || providerId.isNull()) return;
        providerFinished = true;
        auto keepCode = lease;
        session->cancel(providerId);
    }
    Terminal normalize(const ActionResultV2 &result) const
    {
        if (result.action != action || !sameOwner(result.subject, item.ref)) return {{}, invalidError()};
        if (action == SourceActionV2::CreatePlaylist) {
            if (result.subject.entityType != MediaEntityTypeV2::Playlist || result.subject.entityId.trimmed().isEmpty())
                return {{}, invalidError()};
        } else if (result.subject != item.ref) return {{}, invalidError()};
        MediaRefV2 subject{owned(result.subject.sourcePluginId), owned(result.subject.sourceInstanceId),
            owned(result.subject.accountId), result.subject.entityType, owned(result.subject.entityId)};
        QVariantMap out{{"action", int(action)}, {"subject", mediaRefV2ToVariantMap(subject)}};
        switch (action) {
        case SourceActionV2::Favorite: case SourceActionV2::Unfavorite: {
            const auto value = result.payload.value("favorite");
            if (value.metaType().id() != QMetaType::Bool) return {{}, invalidError()};
            out["favorite"] = value.toBool(); break;
        }
        case SourceActionV2::Rating: {
            const auto value = result.payload.value("rating");
            if (!integer(value, 0, 5)) return {{}, invalidError()};
            out["rating"] = value.toInt(); break;
        }
        case SourceActionV2::CreateBookmark: {
            qint64 position;
            if (!integer(result.payload.value("positionMs"), 0, std::numeric_limits<qint64>::max(), &position))
                return {{}, invalidError()};
            out["positionMs"] = position; break;
        }
        case SourceActionV2::Download: {
            const auto value = result.payload.value("destination");
            if (value.metaType().id() != QMetaType::QUrl || value.toUrl() != destination || !localDestination(value.toUrl()))
                return {{}, invalidError()};
            out["destination"] = destination; break;
        }
        case SourceActionV2::CreatePlaylist:
            if (!text(result.payload.value("name"))) return {{}, invalidError()};
            out["name"] = owned(result.payload.value("name").toString()); break;
        case SourceActionV2::UpdatePlaylist: case SourceActionV2::DeletePlaylist: break;
        default: return {{}, invalidError()};
        }
        return {out, {}};
    }
};

MediaActionRouter::MediaActionRouter(SourceRegistry *sources, QObject *parent)
    : QObject(parent), m_sources(sources)
{
    if (!sources) return;
    connect(sources, &SourceRegistry::instanceChanged, this, [this](const QString &instance) {
        const auto requests = m_requests.values();
        for (const auto &r : requests)
            if (r->session && r->item.ref.sourceInstanceId == instance) invalidate(r);
    });
    connect(sources, &QObject::destroyed, this, [this] {
        const auto requests = m_requests.values();
        for (const auto &r : requests) invalidate(r);
    });
    if (sources->pluginManager()) {
        connect(sources->pluginManager(), &PluginManager::pluginChanged, this, [this](const QString &package) {
            if (!m_sources || !m_sources->pluginManager()
                || m_sources->pluginManager()->plugin(package).state == PluginState::Loaded) return;
            const auto requests = m_requests.values();
            for (const auto &r : requests) if (r->package == package) invalidate(r);
        });
    }
}
MediaActionRouter::~MediaActionRouter()
{
    const auto requests = m_requests.values();
    m_requests.clear();
    for (const auto &r : requests) {
        r->abandoned = true;
        disconnectAll(r->connections);
        r->cancel();
    }
}

QUuid MediaActionRouter::setFavorite(const QVariantMap &media, bool favorite)
{
    auto r = std::make_shared<Request>();
    r->action = favorite ? SourceActionV2::Favorite : SourceActionV2::Unfavorite;
    r->favorite = favorite;
    if (!parseItem(media, &r->item)) r->reject(invalidError());
    return submit(r);
}
QUuid MediaActionRouter::setRating(const QVariantMap &media, int rating)
{
    auto r = std::make_shared<Request>(); r->action = SourceActionV2::Rating; r->rating = rating;
    if (!parseItem(media, &r->item) || rating < 0 || rating > 5) r->reject(invalidError());
    return submit(r);
}
QUuid MediaActionRouter::download(const QVariantMap &media, const QUrl &destination)
{
    auto r = std::make_shared<Request>(); r->action = SourceActionV2::Download; r->destination = destination;
    if (!parseItem(media, &r->item) || !localDestination(destination)) r->reject(invalidError());
    return submit(r);
}
QUuid MediaActionRouter::createPlaylist(const QString &instance, const QString &name)
{
    auto r = std::make_shared<Request>(); r->action = SourceActionV2::CreatePlaylist;
    r->name = name; r->item.ref.sourceInstanceId = instance; r->item.ref.entityType = MediaEntityTypeV2::Playlist;
    if (instance.trimmed().isEmpty() || name.trimmed().isEmpty()) r->reject(invalidError());
    return submit(r);
}
QUuid MediaActionRouter::updatePlaylist(const QVariantMap &playlist, const QVariantMap &change)
{
    auto r = std::make_shared<Request>(); r->action = SourceActionV2::UpdatePlaylist;
    bool valid = parseItem(playlist, &r->item) && r->item.ref.entityType == MediaEntityTypeV2::Playlist;
    for (auto it = change.cbegin(); it != change.cend(); ++it) {
        if (it.key() == QLatin1String("newName")) {
            valid &= text(*it); r->change.newName = text(*it) ? it->toString() : QString{};
        } else if (it.key() == QLatin1String("tracksToAdd") && it->metaType().id() == QMetaType::QVariantList) {
            for (const auto &value : it->toList()) {
                Item track;
                if (value.metaType().id() != QMetaType::QVariantMap || !parseItem(value.toMap(), &track)
                    || m_resolver.canAddToPlaylist(r->item.ref, track.ref).state != AvailabilityV2::Available) {
                    valid = false; continue;
                }
                r->additions.append(track); r->change.tracksToAdd.append(track.ref);
            }
        } else if (it.key() == QLatin1String("trackIndexesToRemove") && it->metaType().id() == QMetaType::QVariantList) {
            QSet<int> seen;
            for (const auto &value : it->toList()) {
                if (!integer(value, 0, std::numeric_limits<int>::max()) || seen.contains(value.toInt())) {
                    valid = false; continue;
                }
                seen.insert(value.toInt()); r->change.trackIndexesToRemove.append(value.toInt());
            }
        } else valid = false;
    }
    if (!valid || (r->change.newName.isEmpty() && r->additions.isEmpty() && r->change.trackIndexesToRemove.isEmpty()))
        r->reject(invalidError());
    return submit(r);
}
QUuid MediaActionRouter::deletePlaylist(const QVariantMap &playlist)
{
    auto r = std::make_shared<Request>(); r->action = SourceActionV2::DeletePlaylist;
    if (!parseItem(playlist, &r->item) || r->item.ref.entityType != MediaEntityTypeV2::Playlist) r->reject(invalidError());
    return submit(r);
}
QUuid MediaActionRouter::setBookmark(const QVariantMap &media, qint64 positionMs)
{
    auto r = std::make_shared<Request>(); r->action = SourceActionV2::CreateBookmark; r->positionMs = positionMs;
    if (!parseItem(media, &r->item) || positionMs < 0 || r->item.ref.entityType != MediaEntityTypeV2::Track)
        r->reject(invalidError());
    return submit(r);
}

QUuid MediaActionRouter::submit(std::shared_ptr<Request> r)
{
    const auto id = r->hostId;
    const QPointer<MediaActionRouter> guard(this);
    m_requests.insert(id, r);
    if (!r->settled) dispatch(r);
    r->returned = true;
    if (guard) schedule(r);
    else r->cancel();
    return id;
}

void MediaActionRouter::dispatch(const std::shared_ptr<Request> &r)
{
    const QPointer<MediaActionRouter> guard(this);
    r->registry = m_sources;
    if (!r->registry || !r->registry->pluginManager()) { r->reject(capabilityError(AvailabilityV2::Unavailable)); return; }
    r->plugins = r->registry->pluginManager();
    auto descriptors = r->registry->enabledInstances();
    if (!guard || !r->registry || !r->plugins) return;
    std::optional<SourceInstanceDescriptorV2> owner;
    for (const auto &d : descriptors) if (d.sourceInstanceId == r->item.ref.sourceInstanceId) {
        if (owner) { r->reject(invalidError()); return; }
        owner = d;
    }
    if (!owner || !owner->enabled || r->plugins->plugin(owner->pluginPackageId).state != PluginState::Loaded) {
        r->reject(capabilityError(AvailabilityV2::Unavailable)); return;
    }
    if (r->action == SourceActionV2::CreatePlaylist) {
        r->item.ref.sourcePluginId = owner->sourceId;
        r->item.ref.accountId = owner->accountId;
    }
    if (r->item.ref.sourcePluginId != owner->sourceId || r->item.ref.accountId != owner->accountId) {
        r->reject(invalidError()); return;
    }
    r->package = owner->pluginPackageId;
    // Extra callable lease protects the plugin root even if a callback closes
    // the registry-owned session and releases its independent session lease.
    r->lease = r->plugins->acquire(r->package);
    if (!guard || !r->registry || !r->plugins || !r->lease.isValid()) {
        r->reject(capabilityError(AvailabilityV2::Unavailable)); return;
    }
    r->root = r->plugins->pluginInstance(r->package);
    auto plugin = qobject_cast<IMusicSourcePluginV2 *>(r->root.data());
    if (!plugin) { r->reject(capabilityError(AvailabilityV2::Unsupported)); return; }
    r->session = r->registry->sessionFor(r->item.ref.sourceInstanceId);
    if (!guard || !r->current()) { r->reject(capabilityError(AvailabilityV2::Unavailable)); return; }
    r->connections.append(connect(r->session, &QObject::destroyed, this, [this, r] { invalidate(r); }));
    r->connections.append(connect(r->session, &IMusicSourceSessionV2::stateChanged, this,
        [this, r](SourceSessionStateV2 state) { if (state != SourceSessionStateV2::Ready) invalidate(r); }));
    r->connections.append(connect(r->session, &IMusicSourceSessionV2::capabilitiesChanged, this,
        [this, r](const CapabilitySetV2 &) {
            r->invalidated = true;
            r->reject(capabilityError(AvailabilityV2::Unavailable));
            schedule(r);
        }));
    auto usable = [&] {
        if (!guard || !r->current()) return false;
        const auto list = r->registry->enabledInstances();
        if (!guard || !r->current()) return false;
        for (const auto &d : list) if (d.sourceInstanceId == r->item.ref.sourceInstanceId)
            return d.enabled && d.state == SourceSessionStateV2::Ready && d.pluginPackageId == r->package
                && d.sourceId == r->item.ref.sourcePluginId && d.accountId == r->item.ref.accountId;
        return false;
    };
    if (!usable()) { r->reject(capabilityError(AvailabilityV2::Unavailable)); return; }
    const auto identity = r->session->identity();
    if (!usable()) { r->reject(capabilityError(AvailabilityV2::Unavailable)); return; }
    if (identity.sourcePluginId != r->item.ref.sourcePluginId || identity.sourceInstanceId != r->item.ref.sourceInstanceId
        || identity.accountId != r->item.ref.accountId) { r->reject(invalidError()); return; }
    bool hasProvider = false;
    switch (r->action) {
    case SourceActionV2::Favorite: case SourceActionV2::Unfavorite: hasProvider = qobject_cast<IFavoriteProviderV2 *>(r->session); break;
    case SourceActionV2::Rating: hasProvider = qobject_cast<IRatingProviderV2 *>(r->session); break;
    case SourceActionV2::Download: hasProvider = qobject_cast<IDownloadProviderV2 *>(r->session); break;
    case SourceActionV2::CreatePlaylist: case SourceActionV2::UpdatePlaylist: case SourceActionV2::DeletePlaylist:
        hasProvider = qobject_cast<IPlaylistProviderV2 *>(r->session); break;
    case SourceActionV2::CreateBookmark: hasProvider = qobject_cast<IBookmarkProviderV2 *>(r->session); break;
    default: break;
    }
    if (!hasProvider) { r->reject(capabilityError(AvailabilityV2::Unsupported)); return; }
    const auto declared = plugin->descriptor();
    if (!usable()) { r->reject(capabilityError(AvailabilityV2::Unavailable)); return; }
    if (declared.sourceId != r->item.ref.sourcePluginId || declared.pluginPackageId != r->package) {
        r->reject(invalidError()); return;
    }
    const auto capabilities = r->session->capabilities();
    if (!usable()) { r->reject(capabilityError(AvailabilityV2::Unavailable)); return; }
    const CapabilityResolver resolver = m_resolver;
    auto allowed = [&](SourceActionV2 action, const Item &item, bool objectFree = false) {
        const auto media = objectFree ? ActionAvailabilityV2{AvailabilityV2::Available, {}, {}} : item.actions.value(action);
        auto resolved = resolver.resolve(action, declared.declaredActions.value(action),
            capabilities.serverAction(action), capabilities.accountAction(action), media);
        // None of these provider signatures carries a bitrate limit, so a
        // maximum cannot safely be enforced by this router.
        if (resolved.state == AvailabilityV2::Available && resolved.constraints.contains("maxBitrate"))
            resolved.state = AvailabilityV2::Unavailable;
        if (resolved.state != AvailabilityV2::Available) { r->reject(capabilityError(resolved.state)); return false; }
        return true;
    };
    if (!allowed(r->action, r->item, r->action == SourceActionV2::CreatePlaylist)) return;
    if (r->action == SourceActionV2::UpdatePlaylist) {
        if (!r->additions.isEmpty()) {
            if (!allowed(SourceActionV2::AddPlaylistTracks, r->item)) return;
            for (const auto &track : r->additions) if (!allowed(SourceActionV2::AddPlaylistTracks, track)) return;
        }
        if (!r->change.trackIndexesToRemove.isEmpty() && !allowed(SourceActionV2::RemovePlaylistTracks, r->item)) return;
    }
    // A preceding started observer may destroy the router. Keep ownership
    // capture alive until this provider invocation unwinds, independently of it.
    QObject invocationObserver;
    connect(r->session, &IMusicSourceSessionV2::requestStarted, &invocationObserver, [r](QUuid pid) {
        if (r->invoking && !pid.isNull()) r->started.insert(pid);
    });
    r->connections.append(connect(r->session, &IMusicSourceSessionV2::actionCompleted, this,
        [this, r](QUuid pid, const ActionResultV2 &result) {
            if (pid.isNull() || r->settled) return;
            if (r->invoking) {
                if (!r->inlineTerminals.contains(pid)) r->inlineTerminals.insert(pid, r->normalize(result));
            } else if (pid == r->providerId && r->started.contains(pid)) {
                r->providerFinished = true; r->terminal = r->normalize(result); r->settled = true; schedule(r);
            }
        }));
    r->connections.append(connect(r->session, &IMusicSourceSessionV2::requestFailed, this,
        [this, r](QUuid pid, const SourceErrorV2 &error) {
            if (pid.isNull() || r->settled) return;
            const auto state = error.kind == SourceErrorKindV2::Authorization ? AvailabilityV2::Forbidden
                : error.kind == SourceErrorKindV2::Unsupported ? AvailabilityV2::Unsupported : AvailabilityV2::Unavailable;
            const Request::Terminal terminal{{}, capabilityError(state)};
            if (r->invoking) {
                if (!r->inlineTerminals.contains(pid)) r->inlineTerminals.insert(pid, terminal);
            } else if (pid == r->providerId && r->started.contains(pid)) {
                r->providerFinished = true; r->terminal = terminal; r->settled = true; schedule(r);
            }
        }));
    r->invoking = true;
    QUuid returned;
    switch (r->action) {
    case SourceActionV2::Favorite: case SourceActionV2::Unfavorite:
        returned = qobject_cast<IFavoriteProviderV2 *>(r->session)->setFavorite(r->item.ref, r->favorite); break;
    case SourceActionV2::Rating:
        returned = qobject_cast<IRatingProviderV2 *>(r->session)->setRating(r->item.ref, r->rating); break;
    case SourceActionV2::Download:
        returned = qobject_cast<IDownloadProviderV2 *>(r->session)->download(r->item.ref, r->destination); break;
    case SourceActionV2::CreatePlaylist:
        returned = qobject_cast<IPlaylistProviderV2 *>(r->session)->createPlaylist(r->name, {}); break;
    case SourceActionV2::UpdatePlaylist:
        returned = qobject_cast<IPlaylistProviderV2 *>(r->session)->updatePlaylist(r->item.ref, r->change); break;
    case SourceActionV2::DeletePlaylist:
        returned = qobject_cast<IPlaylistProviderV2 *>(r->session)->deletePlaylist(r->item.ref); break;
    case SourceActionV2::CreateBookmark:
        returned = qobject_cast<IBookmarkProviderV2 *>(r->session)->createBookmark(r->item.ref, r->positionMs, {}); break;
    default: break;
    }
    r->invoking = false;
    // Validate ownership before every lifecycle/abandonment cleanup path.
    r->providerId = r->started.contains(returned) ? returned : QUuid{};
    if (!guard || !r->current()) {
        r->reject(capabilityError(AvailabilityV2::Unavailable));
    } else if (r->providerId.isNull()) {
        r->reject(invalidError());
    } else if (!r->settled && r->inlineTerminals.contains(returned)) {
        r->providerFinished = true; r->terminal = r->inlineTerminals.value(returned); r->settled = true;
    }
    r->inlineTerminals.clear();
    if (r->abandoned) r->cancel();
}

void MediaActionRouter::invalidate(const std::shared_ptr<Request> &r)
{
    r->lifecycleEnded = true;
    r->reject(capabilityError(AvailabilityV2::Unavailable));
    schedule(r);
}
void MediaActionRouter::schedule(const std::shared_ptr<Request> &r)
{
    if (!r->returned || r->invoking || !r->settled || r->queued || r->abandoned) return;
    r->queued = true;
    QTimer::singleShot(0, this, [this, r] {
        const QPointer<MediaActionRouter> guard(this);
        if (!m_requests.remove(r->hostId)) return;
        r->cancel();
        // Keep the callable lease through cancellation, but drop it before
        // notifying consumers. Lease release itself can reenter/destroy us.
        r->lease = {};
        // Keep lifecycle observers connected through the reentrant release.
        disconnectAll(r->connections);
        if (!guard) return;
        if (r->terminal.error.isEmpty() && !r->current())
            r->reject(capabilityError(AvailabilityV2::Unavailable));
        if (r->terminal.error.isEmpty()) emit actionSucceeded(r->hostId, r->terminal.result);
        else emit actionFailed(r->hostId, r->terminal.error);
    });
}
