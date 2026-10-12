#pragma once
#include <QStringList>
#include <QtPlugin>

// Optional package-level declaration, immutable while the package is loaded.
// IDs must refer to Secret fields in IPluginSettingsProviderV2::settingsSchema().
// They remain in validation/storage but are absent from ordinary Host forms and
// can only be mutated through IPluginUiBackendSecretsV1. No secret values here.
class IPluginUiBackendSecretProviderV1 {
public:
    virtual ~IPluginUiBackendSecretProviderV1() = default;
    virtual QStringList backendSecretFieldIds() const = 0;
};
#define QUEMUSIC_PLUGIN_UI_BACKEND_SECRET_PROVIDER_V1_IID "org.quemusic.PluginUI.BackendSecretProvider/1.0"
Q_DECLARE_INTERFACE(IPluginUiBackendSecretProviderV1, QUEMUSIC_PLUGIN_UI_BACKEND_SECRET_PROVIDER_V1_IID)
