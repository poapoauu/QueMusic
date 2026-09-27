#pragma once

#include "PluginUiSettingsBridge.h"
#include "PluginUiHostServices.h"
#include "SourceAccountStore.h"

#include <QPointer>
#include <QSet>

class SourceRegistry;

class HostPluginUiSettingsBridge final : public PluginUiSettingsBridge {
    Q_OBJECT
public:
    HostPluginUiSettingsBridge(SourceAccountStore *store, SourceRegistry *registry,
                               const SourceAccountSaveV2 &request, QObject *parent = nullptr);
    QVariantList sections() const override;
    QVariantMap publicValues() const override;
    bool secretConfigured(const QString &fieldId) const override;
    QUuid savePublicValues(const QVariantMap &values) override;
    QUuid saveSecret(const QString &fieldId, const QString &value) override;
    QUuid clearSecret(const QString &fieldId) override;
    QUuid saveSettings(const QVariantMap &publicValues, const QVariantMap &secretValues) override;
    void invalidate();
private:
    bool validIdentity() const;
    bool isSecretField(const QString &id) const;
    bool isPublicField(const QString &id) const;
    QUuid finish(bool success, const QString &reasonKey);
    void notifyChange();
    SourceAccountStore *m_store = nullptr;
    QPointer<SourceRegistry> m_registry;
    SourceAccountSaveV2 m_request;
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
