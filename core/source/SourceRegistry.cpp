#include "SourceRegistry.h"

#include "SourceAccountStore.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QMetaMethod>
#include <QVariantMap>

#include <algorithm>

namespace {

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

SourceRegistry::SourceRegistry(PluginManager *plugins, SourceAccountStore *accounts,
                               QObject *parent)
    : QObject(parent)
    , m_plugins(plugins)
    , m_accounts(accounts)
{
}

SourceRegistry::~SourceRegistry()
{
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
        const auto session = m_sessions.constFind(instanceId);
        if (session != m_sessions.cend() && session->session != nullptr) {
            descriptor.state = session->session->state();
        }
        descriptors.append(descriptor);
    }
    return descriptors;
}

IMusicSourceSessionV2 *SourceRegistry::sessionFor(const QString &instanceId)
{
    auto existing = m_sessions.find(instanceId);
    if (existing != m_sessions.end()) {
        if (existing->session != nullptr) {
            return existing->session;
        }
        m_sessions.erase(existing);
    }

    if (m_plugins == nullptr || m_accounts == nullptr) {
        return nullptr;
    }
    const std::optional<StoredSourceAccount> stored = accountForInstance(instanceId);
    if (!stored.has_value() || !stored->enabled) {
        return nullptr;
    }
    const std::optional<SourceAccount> account =
        m_accounts->sourceAccount(stored->sourceId, stored->accountId);
    if (!account.has_value()) {
        return nullptr;
    }

    QString packageId;
    IMusicSourcePluginV2 *plugin = nullptr;
    if (!loadedV2PluginForSource(stored->sourceId, &packageId, &plugin)) {
        return nullptr;
    }
    PluginLease lease = m_plugins->acquire(packageId);
    if (!lease.isValid()) {
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
    IMusicSourceSessionV2 *session = plugin->createSession(configuration, this);
    if (session == nullptr) {
        return nullptr;
    }

    SessionEntry entry;
    entry.session = session;
    entry.sessionIdentity = session;
    entry.lease = std::move(lease);
    m_sessions.insert(instanceId, entry);

    connect(session, &IMusicSourceSessionV2::pageReady, this,
            [this, instanceId, session](const QUuid &requestId, const PageResultV2 &) {
                forgetRequest(instanceId, session, requestId);
            });
    connect(session, &IMusicSourceSessionV2::streamReady, this,
            [this, instanceId, session](const QUuid &requestId, const StreamDescriptorV2 &) {
                forgetRequest(instanceId, session, requestId);
            });
    connect(session, &IMusicSourceSessionV2::actionCompleted, this,
            [this, instanceId, session](const QUuid &requestId, const ActionResultV2 &) {
                forgetRequest(instanceId, session, requestId);
            });
    connect(session, &IMusicSourceSessionV2::requestFailed, this,
            [this, instanceId, session](const QUuid &requestId, const SourceErrorV2 &) {
                forgetRequest(instanceId, session, requestId);
            });
    connect(session, &QObject::destroyed, this, [this, instanceId, session] {
        const auto current = m_sessions.find(instanceId);
        if (current != m_sessions.end() && current->sessionIdentity == session) {
            m_sessions.erase(current);
            emit instanceChanged(instanceId);
        }
    });

    const int requestStartedIndex =
        session->metaObject()->indexOfSignal("requestStarted(QUuid)");
    const int trackRequestIndex = metaObject()->indexOfSlot("trackObservableRequest(QUuid)");
    if (requestStartedIndex >= 0 && trackRequestIndex >= 0) {
        QObject::connect(session, session->metaObject()->method(requestStartedIndex), this,
                         metaObject()->method(trackRequestIndex));
    }

    const QUuid openRequest = session->open();
    auto current = m_sessions.find(instanceId);
    if (current == m_sessions.end() || current->sessionIdentity != session
        || current->session == nullptr) {
        return nullptr;
    }
    if (!openRequest.isNull()) {
        current->activeRequests.insert(openRequest);
    }
    connect(session, &IMusicSourceSessionV2::stateChanged, this,
            [this, instanceId, session](SourceSessionStateV2) {
                const auto active = m_sessions.constFind(instanceId);
                if (active != m_sessions.cend() && active->sessionIdentity == session) {
                    emit instanceChanged(instanceId);
                }
            });
    emit instanceChanged(instanceId);
    return session;
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

bool SourceRegistry::disableInstance(const QString &instanceId)
{
    if (m_accounts == nullptr) {
        return false;
    }
    const std::optional<StoredSourceAccount> account = accountForInstance(instanceId);
    if (!account.has_value()) {
        return false;
    }
    if (!account->enabled) {
        return true;
    }
    if (!m_accounts->setEnabled(account->sourceId, account->accountId, false)) {
        return false;
    }
    closeEntry(instanceId, false);
    emit instanceChanged(instanceId);
    return true;
}

bool SourceRegistry::closeInstance(const QString &instanceId)
{
    return closeEntry(instanceId, true);
}

void SourceRegistry::closeAll()
{
    closeAll(true);
}

void SourceRegistry::trackObservableRequest(const QUuid requestId)
{
    if (requestId.isNull()) {
        return;
    }
    QObject *requestingSession = sender();
    for (auto entry = m_sessions.begin(); entry != m_sessions.end(); ++entry) {
        if (entry->sessionIdentity == requestingSession && entry->session != nullptr) {
            entry->activeRequests.insert(requestId);
            return;
        }
    }
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
        const SourceDescriptorV2 descriptor = candidate->descriptor();
        if (descriptor.sourceId != sourceId
            || (!descriptor.pluginPackageId.isEmpty()
                && descriptor.pluginPackageId != candidatePackageId)) {
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

bool SourceRegistry::closeEntry(const QString &instanceId, bool notify)
{
    const auto entry = m_sessions.find(instanceId);
    if (entry == m_sessions.end()) {
        return true;
    }
    if (entry->closing) {
        return true;
    }
    entry->closing = true;

    const QPointer<IMusicSourceSessionV2> session = entry->session;
    if (session != nullptr) {
        QObject::disconnect(session, nullptr, this, nullptr);
        const QSet<QUuid> requests = entry->activeRequests;
        for (const QUuid &requestId : requests) {
            if (session == nullptr) {
                break;
            }
            session->cancel(requestId);
        }
        if (session != nullptr) {
            session->close();
            delete session;
        }
    }
    m_sessions.erase(entry);
    if (notify) {
        emit instanceChanged(instanceId);
    }
    return true;
}

void SourceRegistry::closeAll(bool notify)
{
    QStringList instanceIds = m_sessions.keys();
    std::sort(instanceIds.begin(), instanceIds.end());
    for (const QString &instanceId : instanceIds) {
        closeEntry(instanceId, notify);
    }
}
