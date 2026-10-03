#include "LegacyMediaIdentityResolver.h"

#include "PluginManager.h"
#include "SourceRegistry.h"
#include "extensions/legacy-identity/v1/ILegacyMediaIdentityProviderV1.h"
#include <QSet>
#include <QThread>

namespace {
QString owned(const QString &value)
{
    return QString(value.constData(), value.size());
}

bool currentCandidate(SourceRegistry *registry, const QString &instanceId,
                      const SourceInstanceDescriptorV2 &expected)
{
    if (!registry) return false;
    const auto instances = registry->enabledInstances();
    for (const auto &instance : instances) {
        if (instance.sourceInstanceId == instanceId)
            return instance.enabled && instance.state == SourceSessionStateV2::Ready
                && instance.pluginPackageId == expected.pluginPackageId
                && instance.sourceId == expected.sourceId
                && instance.accountId == expected.accountId;
    }
    return false;
}
}

LegacyMediaIdentityResolver::LegacyMediaIdentityResolver(SourceRegistry *registry)
    : m_registry(registry) {}

LegacyMediaIdentityResolver::Result LegacyMediaIdentityResolver::resolve(
    const QUrl &fileUrl, const QStringList &candidateInstanceIds) const
{
    const auto registry = m_registry;
    if (!registry || QThread::currentThread() != registry->thread()
        || !fileUrl.isValid() || !fileUrl.isLocalFile()
        || !fileUrl.authority().isEmpty() || fileUrl.hasQuery() || fileUrl.hasFragment()
        || candidateInstanceIds.isEmpty()) return {Status::InvalidRequest, {}};

    const auto instances = registry->enabledInstances();
    QSet<QString> candidates;
    for (const auto &id : candidateInstanceIds) {
        if (id.trimmed().isEmpty()) return {Status::InvalidRequest, {}};
        candidates.insert(id);
    }
    QList<MediaRefV2> claims;
    bool unavailable = false;
    for (const auto &instanceId : candidates) {
        if (!registry) return {Status::Unavailable, {}};
        SourceInstanceDescriptorV2 owner;
        int count = 0;
        for (const auto &instance : instances)
            if (instance.sourceInstanceId == instanceId) { owner = instance; ++count; }
        if (count != 1 || !owner.enabled || owner.state != SourceSessionStateV2::Ready
            || owner.pluginPackageId.isEmpty()) { unavailable = true; continue; }
        QPointer<PluginManager> plugins = registry->pluginManager();
        if (!plugins) { unavailable = true; continue; }
        const auto lease = plugins->acquire(owner.pluginPackageId);
        if (!registry || !plugins || !lease.isValid()
            || !currentCandidate(registry, instanceId, owner)) {
            unavailable = true;
            continue;
        }
        QPointer<IMusicSourceSessionV2> session = registry->sessionFor(instanceId);
        if (!registry || !session || session->state() != SourceSessionStateV2::Ready
            || !currentCandidate(registry, instanceId, owner)) {
            unavailable = true;
            continue;
        }
        auto *provider = qobject_cast<ILegacyMediaIdentityProviderV1 *>(session.data());
        if (!provider) { unavailable = true; continue; }
        const auto claim = provider->claimLegacyFile(fileUrl);
        if (!registry || !session || !plugins || !currentCandidate(registry, instanceId, owner)
            || registry->sessionFor(instanceId) != session.data()) {
            unavailable = true;
            continue;
        }
        if (!claim) continue;
        if (claim->sourcePluginId != owner.sourceId
            || claim->sourceInstanceId != owner.sourceInstanceId
            || claim->accountId != owner.accountId
            || claim->entityType != MediaEntityTypeV2::Track
            || claim->entityId.trimmed().isEmpty()) {
            unavailable = true;
            continue;
        }
        claims.append({owned(claim->sourcePluginId), owned(claim->sourceInstanceId),
                       owned(claim->accountId), claim->entityType, owned(claim->entityId)});
    }
    if (claims.size() > 1) return {Status::Ambiguous, {}};
    if (unavailable) return {Status::Unavailable, {}};
    if (claims.size() == 1) return {Status::Matched, claims.first()};
    return {Status::NoMatch, {}};
}
