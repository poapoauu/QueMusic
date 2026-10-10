#include "SourceRegistry.h"

#include "SourceAccountStore.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include "extensions/content-events/v1/ISourceContentEventsProviderV1.h"
#include "extensions/content-events/v1/SourceContentEventsV1.h"

#include <QVariantMap>
#include <QRegularExpression>
#include <QScopeGuard>

#include <algorithm>
#include <memory>
#include <utility>

namespace {

ActionAvailabilityV2 ownedPresentationAction(const ActionAvailabilityV2 &value)
{
    auto result = intersectActionAvailabilityV2({value});
    static const QRegularExpression reasonPattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,127}$"));
    result.reasonKey = reasonPattern.match(result.reasonKey).hasMatch()
        ? QString(result.reasonKey.constData(), result.reasonKey.size()) : QString{};
    // Normalization admits only built-in scalar constraints. Do not retain
    // QStringLiteral keys or custom QVariant metatypes from unloadable code.
    QVariantMap constraints;
    if (result.constraints.contains(QStringLiteral("sameSourceOnly")))
        constraints.insert(QStringLiteral("sameSourceOnly"), result.constraints.value(QStringLiteral("sameSourceOnly")).toBool());
    if (result.constraints.contains(QStringLiteral("maxBitrate")))
        constraints.insert(QStringLiteral("maxBitrate"), result.constraints.value(QStringLiteral("maxBitrate")).toDouble());
    result.constraints = std::move(constraints);
    return result;
}

bool sessionOwnsEvents(QObject *session, SourceContentEventsV1 *events)
{
    if (!session || !events || events->thread() != session->thread()) return false;
    for (QObject *owner = events->parent(); owner; owner = owner->parent()) {
        if (owner == session) return true;
    }
    return false;
}

QList<StoredSourceAccount> sortedAccounts(SourceAccountStore *accounts)
{
    QList<StoredSourceAccount> result = accounts == nullptr ? QList<StoredSourceAccount>{}
                                                            : accounts->accounts();
    std::sort(result.begin(), result.end(),
              [](const StoredSourceAccount &left, const StoredSourceAccount &right) {
                  const QString leftId = sourceInstanceId(left.sourceId, left.accountId);
                  const QString rightId = sourceInstanceId(right.sourceId, right.accountId);
                  if (leftId != rightId) {
                      return leftId < rightId;
                  }
                  if (left.sourceId != right.sourceId) {
                      return left.sourceId < right.sourceId;
                  }
                  return left.accountId < right.accountId;
              });
    return result;
}

}

class SourceRegistry::CreationReservation {
public:
    CreationReservation(SourceRegistry *registry, QString instanceId, quint64 token,
                        quint64 lifecycleGeneration, quint64 instanceGeneration)
        : m_registry(registry)
        , m_instanceId(std::move(instanceId))
        , m_token(token)
        , m_lifecycleGeneration(lifecycleGeneration)
        , m_instanceGeneration(instanceGeneration)
    {
    }

    ~CreationReservation()
    {
        if (m_registry != nullptr) {
            m_registry->releaseCreation(m_instanceId, m_token);
        }
    }

    bool isCurrent() const
    {
        return m_registry != nullptr
            && m_registry->creationIsCurrent(m_instanceId, m_token,
                                             m_lifecycleGeneration,
                                             m_instanceGeneration);
    }

private:
    QPointer<SourceRegistry> m_registry;
    QString m_instanceId;
    quint64 m_token = 0;
    quint64 m_lifecycleGeneration = 0;
    quint64 m_instanceGeneration = 0;
};

SourceRegistry::SourceRegistry(PluginManager *plugins, SourceAccountStore *accounts,
                               QObject *parent)
    : QObject(parent)
    , m_plugins(plugins)
    , m_accounts(accounts)
{
}

SourceRegistry::~SourceRegistry()
{
    m_destroying = true;
    ++m_lifecycleGeneration;
    closeAll(false);
}

PluginManager *SourceRegistry::pluginManager() const
{
    return m_plugins.data();
}

QList<SourceInstanceDescriptorV2> SourceRegistry::enabledInstances() const
{
    QList<SourceInstanceDescriptorV2> descriptors;
    for (const StoredSourceAccount &account : sortedAccounts(m_accounts)) {
        const QString instanceId = sourceInstanceId(account.sourceId, account.accountId);
        SourceInstanceDescriptorV2 descriptor;
        descriptor.pluginPackageId = packageIdForSource(account.sourceId);
        descriptor.sourceId = account.sourceId;
        descriptor.sourceInstanceId = instanceId;
        descriptor.accountId = account.accountId;
        descriptor.displayName = account.displayName;
        descriptor.enabled = account.enabled;
        QPointer<IMusicSourceSessionV2> session;
        {
            const auto current = m_sessions.constFind(instanceId);
            if (current != m_sessions.cend()) {
                session = current->session;
            }
        }
        if (session != nullptr) {
            descriptor.state = session->state();
        }
        descriptors.append(descriptor);
    }
    return descriptors;
}

IMusicSourceSessionV2 *SourceRegistry::sessionFor(const QString &instanceId)
{
    if (m_closingAll || m_destroying || m_closingInstances.contains(instanceId)
        || m_creationReservations.contains(instanceId)) {
        return nullptr;
    }

    QPointer<IMusicSourceSessionV2> existingSession;
    {
        const auto existing = m_sessions.constFind(instanceId);
        if (existing != m_sessions.cend()) {
            existingSession = existing->session;
        }
    }
    if (existingSession != nullptr) {
        if (existingSession->parent() != this) {
            closeEntry(instanceId, true);
            return nullptr;
        }
        return existingSession;
    }

    if (m_plugins == nullptr || m_accounts == nullptr) {
        return nullptr;
    }
    std::unique_ptr<CreationReservation> reservation = reserveCreation(instanceId);
    if (!reservation) {
        return nullptr;
    }
    const std::optional<StoredSourceAccount> stored = accountForInstance(instanceId);
    if (!stored.has_value() || !stored->enabled) {
        return nullptr;
    }
    const std::optional<ResolvedSourceAccountV2> account =
        m_accounts->resolvedAccountV2(stored->sourceId, stored->accountId);
    if (!account.has_value()) {
        return nullptr;
    }

    QString packageId;
    IMusicSourcePluginV2 *plugin = nullptr;
    if (!loadedV2PluginForSource(stored->sourceId, &packageId, &plugin)) {
        return nullptr;
    }
    if (!stored->pluginPackageId.isEmpty() && stored->pluginPackageId != packageId)
        return nullptr;
    if (!reservation->isCurrent()) {
        return nullptr;
    }
    PluginLease lease = m_plugins->acquire(packageId);
    if (!lease.isValid() || !reservation->isCurrent()) {
        lease = {};
        return nullptr;
    }
    // Session ownership may be retired by any later plugin/snapshot callback.
    // Keep the creation call stack and its plugin-backed temporaries callable.
    const PluginLease callLease = lease;

    plugin = qobject_cast<IMusicSourcePluginV2 *>(m_plugins->pluginInstance(packageId));
    if (plugin == nullptr || !reservation->isCurrent()) {
        lease = {};
        return nullptr;
    }
    const SourceDescriptorV2 pluginDescriptor = plugin->descriptor();
    if (!reservation->isCurrent() || pluginDescriptor.sourceId != stored->sourceId
        || (!pluginDescriptor.pluginPackageId.isEmpty()
            && pluginDescriptor.pluginPackageId != packageId)) {
        lease = {};
        return nullptr;
    }

    const SourceConfigurationV2 configuration{
        packageId,
        stored->sourceId,
        instanceId,
        stored->accountId,
        stored->displayName,
        stored->parameters,
        account->secret,
    };
    const QPointer<SourceRegistry> registryGuard(this);
    QPointer<IMusicSourceSessionV2> session = plugin->createSession(configuration, this);
    if (registryGuard == nullptr || session == nullptr || !reservation->isCurrent()) {
        if (session != nullptr) {
            session->close();
            if (session != nullptr) {
                delete session;
            }
        }
        lease = {};
        return nullptr;
    }
    if (session->parent() != this) {
        session->close();
        if (session != nullptr) {
            delete session;
        }
        lease = {};
        return nullptr;
    }
    if (!reservation->isCurrent()) {
        session->close();
        if (session != nullptr) {
            delete session;
        }
        lease = {};
        return nullptr;
    }

    SessionEntry entry;
    entry.session = session;
    entry.sessionIdentity = session.data();
    entry.lease = std::move(lease);
    entry.packageId = packageId;
    m_sessions.insert(instanceId, std::move(entry));

    QObject *const sessionIdentity = session.data();
    connect(session, &IMusicSourceSessionV2::capabilitiesChanged, this,
            [this, instanceId, sessionIdentity](const CapabilitySetV2 &) {
                const QPointer<SourceRegistry> guard(this);
                refreshPresentationCapabilities(instanceId, sessionIdentity);
                if (!guard) return;
                bool active = false;
                {
                    const auto current = m_sessions.constFind(instanceId);
                    active = current != m_sessions.cend() && current->session
                        && current->sessionIdentity == sessionIdentity;
                }
                if (active) emit instanceCapabilitiesChanged(instanceId);
            });
    connect(session, &IMusicSourceSessionV2::requestStarted, this,
            [this, instanceId, sessionIdentity](const QUuid &requestId) {
                trackRequest(instanceId, sessionIdentity, requestId);
            });
    connect(session, &IMusicSourceSessionV2::pageReady, this,
            [this, instanceId, sessionIdentity](const QUuid &requestId,
                                                const PageResultV2 &) {
                forgetRequest(instanceId, sessionIdentity, requestId);
            });
    connect(session, &IMusicSourceSessionV2::streamReady, this,
            [this, instanceId, sessionIdentity](const QUuid &requestId,
                                                const StreamDescriptorV2 &) {
                forgetRequest(instanceId, sessionIdentity, requestId);
            });
    connect(session, &IMusicSourceSessionV2::actionCompleted, this,
            [this, instanceId, sessionIdentity](const QUuid &requestId,
                                                const ActionResultV2 &) {
                forgetRequest(instanceId, sessionIdentity, requestId);
            });
    connect(session, &IMusicSourceSessionV2::requestFailed, this,
            [this, instanceId, sessionIdentity](const QUuid &requestId,
                                                const SourceErrorV2 &) {
                forgetRequest(instanceId, sessionIdentity, requestId);
            });
    connect(session, &IMusicSourceSessionV2::settingsActionCompleted, this,
            [this, instanceId, sessionIdentity](const QUuid &requestId, const QString &) {
                forgetRequest(instanceId, sessionIdentity, requestId);
            });
    connect(session, &QObject::destroyed, this, [this, instanceId, sessionIdentity] {
        handleExternalDestruction(instanceId, sessionIdentity);
    });
    connect(session, &IMusicSourceSessionV2::stateChanged, this,
            [this, instanceId, sessionIdentity](SourceSessionStateV2 state) {
                const QPointer<SourceRegistry> guard(this);
                bool active = false;
                {
                    auto current = m_sessions.find(instanceId);
                    active = current != m_sessions.end() && current->session
                        && current->sessionIdentity == sessionIdentity;
                    if (active) {
                        ++current->presentationRevision;
                        current->presentationActions.clear();
                        current->presentationState = state;
                    }
                }
                if (!active) return;
                // A downgrade invalidates immediately, without querying the
                // plugin again. A Ready notification seeds fresh capabilities.
                if (state == SourceSessionStateV2::Ready) refreshPresentationCapabilities(instanceId, sessionIdentity);
                if (!guard) return;
                const auto current = m_sessions.constFind(instanceId);
                active = current != m_sessions.cend() && current->session && current->sessionIdentity == sessionIdentity;
                if (active) {
                    emit instanceChanged(instanceId);
                }
            });

    bindContentEvents(instanceId, session);
    if (!registryGuard || !session || !reservation->isCurrent()) return nullptr;
    session->open();
    if (registryGuard == nullptr || !reservation->isCurrent()) {
        return nullptr;
    }
    bool active = false;
    {
        const auto current = m_sessions.constFind(instanceId);
        active = current != m_sessions.cend() && current->sessionIdentity == sessionIdentity
            && current->session != nullptr;
    }
    if (!active) {
        return nullptr;
    }
    if (session->parent() != this) {
        closeEntry(instanceId, true);
        return nullptr;
    }
    // Some valid v2 plugins open synchronously without a state/capability
    // notification. Seed their snapshot before publishing the new instance.
    refreshPresentationCapabilities(instanceId, sessionIdentity);
    if (!registryGuard || !session || !reservation->isCurrent()) return nullptr;
    emit instanceChanged(instanceId);
    if (registryGuard == nullptr || !reservation->isCurrent()) {
        return nullptr;
    }
    return session.data();
}

void SourceRegistry::refreshPresentationCapabilities(const QString &instanceId, QObject *identity)
{
    const QPointer<SourceRegistry> guard(this);
    const QPointer<PluginManager> plugins(m_plugins);
    QPointer<IMusicSourceSessionV2> session;
    PluginLease keepCode;
    QString package;
    quint64 revision = 0;
    {
        auto entry = m_sessions.find(instanceId);
        if (entry == m_sessions.end() || entry->sessionIdentity != identity || !entry->session) return;
        session = entry->session;
        keepCode = entry->lease; // Outlives reentrant close/destruction and plugin temporaries.
        package = entry->packageId;
        revision = ++entry->presentationRevision;
        entry->presentationActions.clear();
        entry->presentationState = SourceSessionStateV2::Closed;
        // v2 providers may emit capabilitiesChanged while answering a read.
        // Invalidate the outer revision, but do not recursively call them or
        // schedule an endless retry loop. A later independent event can seed.
        if (entry->presentationReading) return;
        entry->presentationReading = true;
    }
    const auto readingGuard = qScopeGuard([guard, session, instanceId, identity] {
        if (!guard || !session) return;
        auto entry = guard->m_sessions.find(instanceId);
        if (entry != guard->m_sessions.end() && entry->session == session && entry->sessionIdentity == identity)
            entry->presentationReading = false;
    });
    const auto current = [&] {
        if (!guard || !plugins || !session || !keepCode.isValid() || session->parent() != guard) return false;
        const auto entry = guard->m_sessions.constFind(instanceId);
        return entry != guard->m_sessions.cend() && entry->sessionIdentity == identity
            && entry->session == session && entry->presentationRevision == revision;
    };
    if (!current()) return;
    const auto state = session->state();
    if (!current()) return;
    m_sessions[instanceId].presentationState = state;
    if (state != SourceSessionStateV2::Ready) return;
    const auto account = accountForInstance(instanceId);
    if (!account || !account->enabled || (!account->pluginPackageId.isEmpty() && account->pluginPackageId != package)) return;
    const auto owner = session->identity();
    if (!current() || owner.sourcePluginId != account->sourceId
        || owner.sourceInstanceId != instanceId || owner.accountId != account->accountId) return;
    const QPointer<QObject> root(plugins->pluginInstance(package));
    auto *plugin = qobject_cast<IMusicSourcePluginV2 *>(root.data());
    if (!plugin) return;
    const auto descriptor = plugin->descriptor();
    if (!current() || !root || plugins->pluginInstance(package) != root
        || descriptor.sourceId != account->sourceId || descriptor.pluginPackageId != package) return;
    const auto capabilities = session->capabilities();
    if (!current()) return;
    QHash<SourceActionV2, ActionAvailabilityV2> actions;
    for (int i = 0; i <= int(SourceActionV2::DeleteBookmark); ++i) {
        const auto action = SourceActionV2(i);
        auto effective = intersectActionAvailabilityV2({descriptor.declaredActions.value(action),
            capabilities.serverAction(action), capabilities.accountAction(action)});
        const bool provider = action == SourceActionV2::Play ? bool(qobject_cast<IPlaybackProviderV2 *>(session.data()))
            : action == SourceActionV2::Favorite || action == SourceActionV2::Unfavorite ? bool(qobject_cast<IFavoriteProviderV2 *>(session.data()))
            : action == SourceActionV2::Download ? bool(qobject_cast<IDownloadProviderV2 *>(session.data())) : true;
        if (!provider) effective = intersectActionAvailabilityV2({effective, {AvailabilityV2::Unsupported, {}, {}}});
        actions.insert(action, ownedPresentationAction(effective));
    }
    if (current()) m_sessions[instanceId].presentationActions = std::move(actions);
}

std::optional<SourceInstanceDescriptorV2> SourceRegistry::presentationInstance(const MediaRefV2 &media) const
{
    const auto account = accountForInstance(media.sourceInstanceId);
    if (!account || account->sourceId != media.sourcePluginId || account->accountId != media.accountId) return std::nullopt;
    SourceInstanceDescriptorV2 result;
    result.pluginPackageId = packageIdForSource(account->sourceId);
    result.sourceId = account->sourceId;
    result.sourceInstanceId = media.sourceInstanceId;
    result.accountId = account->accountId;
    result.displayName = account->displayName;
    result.enabled = account->enabled;
    const auto entry = m_sessions.constFind(media.sourceInstanceId);
    if (m_plugins && entry != m_sessions.cend() && entry->session && entry->session->parent() == this
        && entry->lease.isValid() && entry->packageId == result.pluginPackageId
        && (account->pluginPackageId.isEmpty() || account->pluginPackageId == entry->packageId))
        result.state = entry->presentationState;
    return result;
}

ActionAvailabilityV2 SourceRegistry::presentationAction(const MediaRefV2 &media, SourceActionV2 action,
                                                       const ActionAvailabilityV2 &mediaAction) const
{
    ActionAvailabilityV2 source{AvailabilityV2::Unavailable, QStringLiteral("music.actionUnavailable"), {}};
    const auto owner = presentationInstance(media);
    const auto entry = m_sessions.constFind(media.sourceInstanceId);
    if (owner && owner->enabled && owner->state == SourceSessionStateV2::Ready && entry != m_sessions.cend())
        source = entry->presentationActions.value(action, source);
    auto result = ownedPresentationAction(intersectActionAvailabilityV2({source, mediaAction}));
    // The current Core/Router cannot enforce bitrate restrictions. Presentation
    // must not advertise an action that its live execution will reject.
    if (result.state == AvailabilityV2::Available && result.constraints.contains(QStringLiteral("maxBitrate"))) {
        result.state = AvailabilityV2::Unavailable;
        result.reasonKey = QStringLiteral("music.actionConstraintsUnsupported");
    }
    return result;
}

bool SourceRegistry::enableInstance(const QString &instanceId)
{
    if (m_accounts == nullptr) {
        return false;
    }
    const std::optional<StoredSourceAccount> account = accountForInstance(instanceId);
    if (!account.has_value()) {
        return false;
    }
    if (account->enabled) {
        return true;
    }
    if (!m_accounts->setEnabled(account->sourceId, account->accountId, true)) {
        return false;
    }
    emit instanceChanged(instanceId);
    return true;
}

bool SourceRegistry::contentBindingIsCurrent(const QString &instanceId, QObject *session,
                                            SourceContentEventsV1 *events) const
{
    const auto current = m_sessions.constFind(instanceId);
    return current != m_sessions.cend() && current->sessionIdentity == session
        && current->session && current->contentEvents == events
        && sessionOwnsEvents(session, events);
}

void SourceRegistry::bindContentEvents(const QString &instanceId, IMusicSourceSessionV2 *session)
{
    // Retain the callable package across the optional getter and connectNotify.
    // Reentrant close/destruction may remove the registry's entry meanwhile.
    PluginLease callLease;
    {
        const auto current = m_sessions.constFind(instanceId);
        if (current == m_sessions.cend() || current->sessionIdentity != session) return;
        callLease = current->lease;
    }
    auto *provider = qobject_cast<ISourceContentEventsProviderV1 *>(session);
    if (!provider) return; // Old v2 plugins have no dependency on this extension.
    const QPointer<SourceRegistry> guard(this);
    const QPointer<IMusicSourceSessionV2> sessionGuard(session);
    SourceContentEventsV1 *rawEvents = provider->contentEvents();
    if (!guard || !sessionGuard) return;
    const QPointer<SourceContentEventsV1> events = rawEvents;
    if (!events || !sessionOwnsEvents(sessionGuard, events)) return;
    {
        auto entry = m_sessions.find(instanceId);
        if (entry == m_sessions.end() || entry->sessionIdentity != sessionGuard.data()) return;
        entry->contentEvents = events;
    }
    const auto changedConnection = connect(events, &SourceContentEventsV1::contentChanged, this,
        [this, instanceId, sessionGuard, events](quint64 revision) {
            // Captured QPointers fence queued deliveries even if a new session
            // is later allocated at the exact same addresses (ABA).
            if (!sessionGuard || !events
                || !contentBindingIsCurrent(instanceId, sessionGuard, events)) return;
            {
                auto current = m_sessions.find(instanceId);
                if (revision <= current->lastContentRevision) return;
                current->lastContentRevision = revision;
            }
            emit instanceContentChanged(instanceId, revision);
        });
    if (!guard || !sessionGuard || !events
        || !contentBindingIsCurrent(instanceId, sessionGuard, events)) {
        QObject::disconnect(changedConnection);
        return;
    }
    m_sessions[instanceId].contentChangedConnection = changedConnection;
    const auto failedConnection = connect(events, &SourceContentEventsV1::refreshFailed, this,
        [this, instanceId, sessionGuard, events](SourceErrorV2 error) {
            if (!sessionGuard || !events
                || !contentBindingIsCurrent(instanceId, sessionGuard, events)) return;
            emit instanceRefreshFailed(instanceId, error);
        });
    if (!guard || !sessionGuard || !events
        || !contentBindingIsCurrent(instanceId, sessionGuard, events)) {
        QObject::disconnect(failedConnection);
        return;
    }
    m_sessions[instanceId].refreshFailedConnection = failedConnection;
}

bool SourceRegistry::disableInstance(const QString &instanceId)
{
    if (m_accounts == nullptr) {
        return false;
    }
    const std::optional<StoredSourceAccount> account = accountForInstance(instanceId);
    if (!account.has_value()) {
        return false;
    }
    invalidateInstanceCreation(instanceId);
    if (!account->enabled) {
        return true;
    }
    if (!m_accounts->setEnabled(account->sourceId, account->accountId, false)) {
        return false;
    }
    const QPointer<SourceRegistry> guard(this);
    closeEntry(instanceId, false);
    if (guard) emit guard->instanceChanged(instanceId);
    return true;
}

bool SourceRegistry::closeInstance(const QString &instanceId)
{
    return closeEntry(instanceId, true);
}

bool SourceRegistry::configurationChanged(const QString &instanceId)
{
    const auto separator = instanceId.indexOf('/');
    if (separator <= 0 || separator == instanceId.size() - 1 || m_destroying)
        return false;
    if (m_closingInstances.contains(instanceId)) {
        invalidateInstanceCreation(instanceId);
        return true;
    }
    const QPointer<SourceRegistry> guard(this);
    closeEntry(instanceId, false);
    if (!guard) return true;
    m_closingInstances.insert(instanceId);
    emit instanceChanged(instanceId);
    if (guard) guard->m_closingInstances.remove(instanceId);
    return true;
}

void SourceRegistry::closeAll()
{
    closeAll(true);
}

void SourceRegistry::trackRequest(const QString &instanceId, QObject *session,
                                  const QUuid &requestId)
{
    if (requestId.isNull()) {
        return;
    }
    const auto entry = m_sessions.find(instanceId);
    if (entry != m_sessions.end() && entry->sessionIdentity == session
        && entry->session != nullptr) {
        entry->activeRequests.insert(requestId);
    }
}

std::unique_ptr<SourceRegistry::CreationReservation> SourceRegistry::reserveCreation(
    const QString &instanceId)
{
    if (m_closingAll || m_destroying || m_closingInstances.contains(instanceId)
        || m_creationReservations.contains(instanceId)) {
        return {};
    }
    ++m_nextCreationToken;
    if (m_nextCreationToken == 0) {
        ++m_nextCreationToken;
    }
    const quint64 token = m_nextCreationToken;
    const quint64 instanceGeneration = m_instanceLifecycleGenerations.value(instanceId);
    m_creationReservations.insert(instanceId, token);
    return std::make_unique<CreationReservation>(this, instanceId, token,
                                                 m_lifecycleGeneration,
                                                 instanceGeneration);
}

bool SourceRegistry::creationIsCurrent(const QString &instanceId, quint64 token,
                                       quint64 lifecycleGeneration,
                                       quint64 instanceGeneration) const
{
    return !m_closingAll && !m_destroying && !m_closingInstances.contains(instanceId)
        && m_lifecycleGeneration == lifecycleGeneration
        && m_instanceLifecycleGenerations.value(instanceId) == instanceGeneration
        && m_creationReservations.value(instanceId) == token;
}

void SourceRegistry::releaseCreation(const QString &instanceId, quint64 token)
{
    if (m_creationReservations.value(instanceId) == token) {
        m_creationReservations.remove(instanceId);
    }
}

void SourceRegistry::invalidateInstanceCreation(const QString &instanceId)
{
    quint64 &generation = m_instanceLifecycleGenerations[instanceId];
    ++generation;
}

std::optional<StoredSourceAccount> SourceRegistry::accountForInstance(
    const QString &instanceId) const
{
    std::optional<StoredSourceAccount> match;
    for (const StoredSourceAccount &account : sortedAccounts(m_accounts)) {
        if (sourceInstanceId(account.sourceId, account.accountId) != instanceId) {
            continue;
        }
        if (match.has_value()) {
            return std::nullopt;
        }
        match = account;
    }
    return match;
}

QString SourceRegistry::packageIdForSource(const QString &sourceId) const
{
    if (m_plugins == nullptr) {
        return {};
    }
    for (const QVariant &pluginValue : m_plugins->plugins()) {
        const QVariantMap plugin = pluginValue.toMap();
        if (plugin.value(QStringLiteral("sourceId")).toString() == sourceId) {
            return plugin.value(QStringLiteral("id")).toString();
        }
    }
    return {};
}

bool SourceRegistry::loadedV2PluginForSource(const QString &sourceId, QString *packageId,
                                             IMusicSourcePluginV2 **plugin) const
{
    if (m_plugins == nullptr || packageId == nullptr || plugin == nullptr) {
        return false;
    }
    for (const QVariant &pluginValue : m_plugins->plugins()) {
        const QVariantMap spec = pluginValue.toMap();
        if (spec.value(QStringLiteral("sourceId")).toString() != sourceId
            || spec.value(QStringLiteral("state")).toString() != QStringLiteral("loaded")) {
            continue;
        }
        const QString candidatePackageId = spec.value(QStringLiteral("id")).toString();
        auto *candidate =
            qobject_cast<IMusicSourcePluginV2 *>(m_plugins->pluginInstance(candidatePackageId));
        if (candidate == nullptr) {
            continue;
        }
        *packageId = candidatePackageId;
        *plugin = candidate;
        return true;
    }
    return false;
}

void SourceRegistry::forgetRequest(const QString &instanceId, QObject *session,
                                   const QUuid &requestId)
{
    const auto entry = m_sessions.find(instanceId);
    if (entry != m_sessions.end() && entry->sessionIdentity == session) {
        entry->activeRequests.remove(requestId);
    }
}

void SourceRegistry::handleExternalDestruction(const QString &instanceId, QObject *session)
{
    bool isCurrentSession = false;
    {
        const auto current = m_sessions.constFind(instanceId);
        isCurrentSession = current != m_sessions.cend()
            && current->sessionIdentity == session;
    }
    if (!isCurrentSession) {
        return;
    }

    m_closingInstances.insert(instanceId);
    SessionEntry entry = m_sessions.take(instanceId);
    QObject::disconnect(entry.contentChangedConnection);
    QObject::disconnect(entry.refreshFailedConnection);
    const QPointer<SourceRegistry> registry(this);
    if (m_plugins != nullptr) {
        m_plugins->pinLoadedPackage(entry.packageId);
    }
    // The package pin, not this session's lease count, now guarantees safety.
    entry.lease = {};
    if (registry != nullptr) {
        emit registry->instanceCapabilitiesChanged(instanceId);
        if (!registry) return;
        emit registry->instanceChanged(instanceId);
    }
}

bool SourceRegistry::closeEntry(const QString &instanceId, bool notify)
{
    invalidateInstanceCreation(instanceId);
    if (m_closingInstances.contains(instanceId)) {
        return true;
    }
    if (!m_sessions.contains(instanceId)) {
        return true;
    }

    m_closingInstances.insert(instanceId);
    SessionEntry entry = m_sessions.take(instanceId);
    QObject::disconnect(entry.contentChangedConnection);
    QObject::disconnect(entry.refreshFailedConnection);
    const QPointer<SourceRegistry> guard(this);
    const QPointer<IMusicSourceSessionV2> session = entry.session;
    if (session != nullptr) {
        QObject::disconnect(session, nullptr, this, nullptr);
        // A callback may destroy the registry inside cancel/close. Keep this
        // session and its callable lease alive until those invocations unwind.
        session->setParent(nullptr);
        const QSet<QUuid> requests = entry.activeRequests;
        for (const QUuid &requestId : requests) {
            if (session == nullptr) {
                break;
            }
            session->cancel(requestId);
        }
        if (session != nullptr) {
            session->close();
            if (session) delete session;
        }
    }
    entry.lease = {};
    if (!guard) return true;
    m_closingInstances.remove(instanceId);
    if (!m_destroying) emit instanceCapabilitiesChanged(instanceId);
    if (!guard) return true;
    if (notify) {
        emit instanceChanged(instanceId);
    }
    return true;
}

void SourceRegistry::closeAll(bool notify)
{
    ++m_lifecycleGeneration;
    if (m_closingAll) {
        return;
    }
    m_closingAll = true;
    const QPointer<SourceRegistry> guard(this);
    QStringList instanceIds = m_sessions.keys();
    std::sort(instanceIds.begin(), instanceIds.end());
    for (const QString &instanceId : instanceIds) {
        closeEntry(instanceId, notify);
        if (!guard) return;
    }
    if (!m_destroying) {
        m_closingAll = false;
    }
}
