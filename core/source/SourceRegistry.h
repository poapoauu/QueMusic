#pragma once

#include "PluginManager.h"
#include "v2/IMusicSourceSessionV2.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <memory>
#include <optional>
#include <utility>

class IMusicSourcePluginV2;
class SourceAccountStore;
struct StoredSourceAccount;

inline QString sourceInstanceId(const QString &sourceId, const QString &accountId)
{
    return sourceId + QLatin1Char('/') + accountId;
}

struct SourceInstanceDescriptorV2 {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    QString displayName;
    bool enabled = true;
    SourceSessionStateV2 state = SourceSessionStateV2::Closed;
};
Q_DECLARE_METATYPE(SourceInstanceDescriptorV2)

class SourceRegistry : public QObject {
    Q_OBJECT

public:
    explicit SourceRegistry(PluginManager *plugins, SourceAccountStore *accounts,
                            QObject *parent = nullptr);
    ~SourceRegistry() override;

    PluginManager *pluginManager() const;
    QList<SourceInstanceDescriptorV2> enabledInstances() const;
    // Borrowed pointer. The registry owns every returned session and its plugin lease.
    IMusicSourceSessionV2 *sessionFor(const QString &sourceInstanceId);
    bool enableInstance(const QString &sourceInstanceId);
    bool disableInstance(const QString &sourceInstanceId);
    bool closeInstance(const QString &sourceInstanceId);
    void closeAll();

signals:
    void instanceChanged(QString sourceInstanceId);

private:
    class CreationReservation;

    struct DeferredLeaseRelease {
        explicit DeferredLeaseRelease(PluginLease pluginLease)
            : lease(std::move(pluginLease))
        {
        }

        void release() { lease = {}; }

        PluginLease lease;
    };

    struct SessionEntry {
        QPointer<IMusicSourceSessionV2> session;
        QObject *sessionIdentity = nullptr;
        PluginLease lease;
        QSet<QUuid> activeRequests;
    };

    std::optional<StoredSourceAccount> accountForInstance(
        const QString &sourceInstanceId) const;
    QString packageIdForSource(const QString &sourceId) const;
    bool loadedV2PluginForSource(const QString &sourceId, QString *packageId,
                                 IMusicSourcePluginV2 **plugin) const;
    void forgetRequest(const QString &sourceInstanceId, QObject *session,
                       const QUuid &requestId);
    void trackRequest(const QString &sourceInstanceId, QObject *session,
                      const QUuid &requestId);
    std::unique_ptr<CreationReservation> reserveCreation(
        const QString &sourceInstanceId);
    bool creationIsCurrent(const QString &sourceInstanceId, quint64 token,
                           quint64 lifecycleGeneration,
                           quint64 instanceGeneration) const;
    void releaseCreation(const QString &sourceInstanceId, quint64 token);
    void invalidateInstanceCreation(const QString &sourceInstanceId);
    void handleExternalDestruction(const QString &sourceInstanceId, QObject *session);
    void finishDeferredDestruction(const QString &sourceInstanceId, quint64 token);
    bool closeEntry(const QString &sourceInstanceId, bool notify);
    void closeAll(bool notify);

    QPointer<PluginManager> m_plugins;
    SourceAccountStore *m_accounts = nullptr;
    QHash<QString, SessionEntry> m_sessions;
    QHash<QString, quint64> m_creationReservations;
    QHash<QString, quint64> m_instanceLifecycleGenerations;
    QHash<quint64, std::shared_ptr<DeferredLeaseRelease>> m_deferredLeaseReleases;
    QHash<QString, quint64> m_deferredReleaseTokens;
    QSet<QString> m_closingInstances;
    quint64 m_lifecycleGeneration = 0;
    quint64 m_nextCreationToken = 0;
    quint64 m_nextDeferredReleaseToken = 0;
    bool m_closingAll = false;
    bool m_destroying = false;
};
