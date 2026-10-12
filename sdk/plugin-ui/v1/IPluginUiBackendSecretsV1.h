#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <QtPlugin>
#include <optional>

// Optional, independently versioned C++ service on the settings bridge. No QML
// registration/invokables. Only Host-bound, declared backend-only Secret fields
// are accessible; this is not arbitrary storage or proof of authentication.
// Records are bounded (64 KiB), nonempty, lossless UTF-8. Completion uses the
// existing PluginUiSettingsBridge::operationFinished signal. The backend must
// stop using this service when its context is invalidated.
class IPluginUiBackendSecretsV1 {
public:
    virtual ~IPluginUiBackendSecretsV1() = default;
    virtual bool backendSecretConfigured(const QString &fieldId) const = 0;
    virtual std::optional<QByteArray> readBackendSecret(const QString &fieldId) const = 0;
    virtual QUuid saveBackendSecret(const QString &fieldId, const QByteArray &record) = 0;
    virtual QUuid clearBackendSecret(const QString &fieldId) = 0;
};

#define QUEMUSIC_PLUGIN_UI_BACKEND_SECRETS_V1_IID "org.quemusic.PluginUI.BackendSecrets/1.0"
Q_DECLARE_INTERFACE(IPluginUiBackendSecretsV1, QUEMUSIC_PLUGIN_UI_BACKEND_SECRETS_V1_IID)
