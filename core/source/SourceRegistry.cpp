#include "SourceRegistry.h"

#include "SourceAccountStore.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <memory>

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
    m_destroying = true;
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
        || m_creatingInstances.contains(instanceId)) {
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
    m_creatingInstances.insert(instanceId);
    QPointer<IMusicSourceSessionV2> session = plugin->createSession(configuration, this);
    if (session == nullptr) {
        lease = {};
        m_creatingInstances.remove(instanceId);
        return nullptr;
    }
    if (session->parent() != this) {
        session->close();
        if (session != nullptr) {
            delete session;
        }
        lease = {};
        m_creatingInstances.remove(instanceId);
        return nullptr;
    }

    SessionEntry entry;
    entry.session = session;
    entry.sessionIdentity = session.data();
    entry.lease = std::move(lease);
    m_sessions.insert(instanceId, std::move(entry));
    m_creatingInstances.remove(instanceId);

    QObject *const sessionIdentity = session.data();
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
    connect(session, &QObject::destroyed, this, [this, instanceId, sessionIdentity] {
        handleExternalDestruction(instanceId, sessionIdentity);
    });
    connect(session, &IMusicSourceSessionV2::stateChanged, this,
            [this, instanceId, sessionIdentity](SourceSessionStateV2) {
                bool active = false;
                {
                    const auto current = m_sessions.constFind(instanceId);
                    active = current != m_sessions.cend()
                        && current->sessionIdentity == sessionIdentity;
                }
                if (active) {
                    emit instanceChanged(instanceId);
                }
            });

    session->open();
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
    auto deferredLease = std::make_shared<PluginLease>(std::move(entry.lease));
    const QPointer<SourceRegistry> registry(this);
    QObject *const dispatcher = QCoreApplication::instance();
    if (dispatcher == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(
        dispatcher,
        [registry, instanceId, deferredLease] {
            *deferredLease = {};
            if (registry != nullptr) {
                registry->finishDeferredDestruction(instanceId);
            }
        },
        Qt::QueuedConnection);
}

void SourceRegistry::finishDeferredDestruction(const QString &instanceId)
{
    m_closingInstances.remove(instanceId);
    if (!m_destroying) {
        emit instanceChanged(instanceId);
    }
}

bool SourceRegistry::closeEntry(const QString &instanceId, bool notify)
{
    if (m_closingInstances.contains(instanceId)) {
        return true;
    }
    if (!m_sessions.contains(instanceId)) {
        return true;
    }

    m_closingInstances.insert(instanceId);
    SessionEntry entry = m_sessions.take(instanceId);
    const QPointer<IMusicSourceSessionV2> session = entry.session;
    if (session != nullptr) {
        QObject::disconnect(session, nullptr, this, nullptr);
        const QSet<QUuid> requests = entry.activeRequests;
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
    entry.lease = {};
    m_closingInstances.remove(instanceId);
    if (notify) {
        emit instanceChanged(instanceId);
    }
    return true;
}

void SourceRegistry::closeAll(bool notify)
{
    if (m_closingAll) {
        return;
    }
    m_closingAll = true;
    QStringList instanceIds = m_sessions.keys();
    std::sort(instanceIds.begin(), instanceIds.end());
    for (const QString &instanceId : instanceIds) {
        closeEntry(instanceId, notify);
    }
    if (!m_destroying) {
        m_closingAll = false;
    }
}
