#pragma once

#include "PluginUiSettingsBridge.h"
#include "PluginUiHostServices.h"
#include "plugin-ui/v1/IPluginUiBackendSecretsV1.h"
#include "SourceAccountStore.h"

#include <QPointer>
#include <QSet>

class SourceRegistry;

class HostPluginUiSettingsBridge final : public PluginUiSettingsBridge, public IPluginUiBackendSecretsV1 {
    Q_OBJECT
    Q_INTERFACES(IPluginUiBackendSecretsV1)
public:
    HostPluginUiSettingsBridge(SourceAccountStore *store, SourceRegistry *registry,
                               const SourceAccountSaveV2 &request, QObject *parent = nullptr);
    HostPluginUiSettingsBridge(SourceAccountStore *store, SourceRegistry *registry,
                               const SourceAccountSaveV2 &request,
                               const QStringList &backendSecretFields, QObject *parent = nullptr);
    QVariantList sections() const override;
    QVariantMap publicValues() const override;
    bool secretConfigured(const QString &fieldId) const override;
    QUuid savePublicValues(const QVariantMap &values) override;
    QUuid saveSecret(const QString &fieldId, const QString &value) override;
    QUuid clearSecret(const QString &fieldId) override;
    QUuid saveSettings(const QVariantMap &publicValues, const QVariantMap &secretValues) override;
    bool backendSecretConfigured(const QString &fieldId) const override;
    std::optional<QByteArray> readBackendSecret(const QString &fieldId) const override;
    QUuid saveBackendSecret(const QString &fieldId, const QByteArray &record) override;
    QUuid clearBackendSecret(const QString &fieldId) override;
    void invalidate();
private:
    bool validIdentity() const;
    bool isSecretField(const QString &id) const;
    bool isPublicField(const QString &id) const;
    QUuid finish(bool success, const QString &reasonKey);
    void notifyChange();
    QUuid persistSettings(const QVariantMap &values, const QVariantMap &secrets);
    QUuid clearStoredSecret(const QString &fieldId);
    SourceAccountStore *m_store = nullptr;
    QPointer<SourceRegistry> m_registry;
    SourceAccountSaveV2 m_request;
    SettingsSchemaV2 m_presentationSchema;
    QSet<QString> m_backendSecretFields;
    bool m_valid = true;
    bool m_existingRequired = false;
};

class HostPluginUiHostServices final : public PluginUiHostServices {
    Q_OBJECT
public:
    using PluginUiHostServices::PluginUiHostServices;
    QUuid requestDirectory() override;
    void notify(const QString &message, bool isError) override;
    Q_INVOKABLE void completeDirectory(QUuid requestId, const QUrl &localDirectory);
    void invalidate();
private:
    QSet<QUuid> m_pending;
    bool m_valid = true;
};
