#pragma once

#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

#include <QObject>

#if defined(QUEMUSIC_UI_FIXTURE_NO_PROVIDER)
class PluginUiFixturePlugin final : public QObject, public IMusicSourcePluginV2 {
#else
class PluginUiFixturePlugin final : public QObject, public IMusicSourcePluginV2,
                                    public ISourceManagementUiProvider, public IPluginSettingsProviderV2 {
#endif
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
#if defined(QUEMUSIC_UI_FIXTURE_NO_PROVIDER)
    Q_INTERFACES(IMusicSourcePluginV2)
#else
    Q_INTERFACES(IMusicSourcePluginV2 ISourceManagementUiProvider IPluginSettingsProviderV2)
#endif
public:
    SourceDescriptorV2 descriptor() const override;
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &, QObject *parent) override;
#if !defined(QUEMUSIC_UI_FIXTURE_NO_PROVIDER)
    SettingsSchemaV2 settingsSchema() const override;
    ManagementUiDescriptor managementUi() const override;
    QObject *createManagementBackend(const PluginUiContextData &, QObject *parent) override;
#endif
};
