#pragma once

#include "PluginManager.h"
#include "PluginUiContext.h"
#include "v2/SourceV2Types.h"

#include <QObject>
#include <QPointer>
#include <QUrl>

class SourceAccountStore;
class SourceRegistry;
class HostPluginUiSettingsBridge;
class HostPluginUiHostServices;

// Host-private lifecycle owner. The QML Loader item must be cleared between
// invalidateContext() and release() so plugin code cannot outlive its lease.
class PluginManagementUiSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl componentUrl READ componentUrl NOTIFY changed)
    Q_PROPERTY(PluginUiContext *context READ context NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY changed)
public:
    PluginManagementUiSession(PluginManager *manager, SourceAccountStore *store,
                              SourceRegistry *registry, const PluginUiContextData &identity,
                              const SettingsSchemaV2 &schema, QObject *parent = nullptr);
    ~PluginManagementUiSession() override;
    QUrl componentUrl() const { return m_componentUrl; }
    PluginUiContext *context() const { return m_context; }
    QString state() const { return m_state; }
    QString errorKey() const { return m_errorKey; }
    Q_INVOKABLE void invalidateContext();
    Q_INVOKABLE void release();
signals:
    void changed();
private:
    void fail(const QString &key);
    QUrl m_componentUrl;
    QPointer<PluginUiContext> m_context;
    QPointer<HostPluginUiSettingsBridge> m_settings;
    QPointer<HostPluginUiHostServices> m_host;
    QPointer<QObject> m_backend;
    PluginLease m_lease;
    QString m_state = QStringLiteral("error");
    QString m_errorKey;
    bool m_released = false;
};
