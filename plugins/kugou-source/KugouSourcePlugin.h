#pragma once
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include "plugin-ui/v1/IPluginUiBackendSecretProviderV1.h"

// Experimental factory. Credential storage is not service authentication.
class KugouSourcePlugin final : public QObject, public IMusicSourcePluginV2,
                                public IPluginSettingsProviderV2,
                                public IPluginUiBackendSecretProviderV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID FILE "plugin.json")
    Q_INTERFACES(IMusicSourcePluginV2 IPluginSettingsProviderV2 IPluginUiBackendSecretProviderV1)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI; }
    SourceDescriptorV2 descriptor() const override;
    SettingsSchemaV2 settingsSchema() const override;
    QStringList backendSecretFieldIds() const override { return {QStringLiteral("credentialBundle")}; }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                        QObject *parent) override;
};
