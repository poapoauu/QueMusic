#pragma once

#include "PluginManager.h"
#include "v2/IMusicSourceSessionV2.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <optional>

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
    void handleExternalDestruction(const QString &sourceInstanceId, QObject *session);
    void finishDeferredDestruction(const QString &sourceInstanceId);
    bool closeEntry(const QString &sourceInstanceId, bool notify);
    void closeAll(bool notify);

    QPointer<PluginManager> m_plugins;
    SourceAccountStore *m_accounts = nullptr;
    QHash<QString, SessionEntry> m_sessions;
    QSet<QString> m_closingInstances;
    QSet<QString> m_creatingInstances;
    bool m_closingAll = false;
    bool m_destroying = false;
};
