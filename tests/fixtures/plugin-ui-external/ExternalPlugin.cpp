#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include "../PluginUiQrBackend.h"

#include <QObject>

class ExternalPlugin final : public QObject, public IMusicSourcePluginV2,
                             public ISourceManagementUiProvider, public IPluginSettingsProviderV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2 ISourceManagementUiProvider IPluginSettingsProviderV2)
public:
    SettingsSchemaV2 settingsSchema() const override {
        SettingsFieldV2 token;
        token.id = QStringLiteral("token"); token.type = SettingsFieldTypeV2::Secret;
        return {{QStringLiteral("auth"), QStringLiteral("plugin.ui.auth"), {token}}};
    }
    SourceDescriptorV2 descriptor() const override
    {
        SourceDescriptorV2 descriptor;
        descriptor.pluginPackageId = QStringLiteral("org.quemusic.source.external-ui-fixture");
        descriptor.sourceId = QStringLiteral("external-ui-fixture");
        descriptor.name = QStringLiteral("External UI fixture");
        descriptor.version = QStringLiteral("1.0.0");
        descriptor.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
        return descriptor;
    }

    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &, QObject *) override
    {
        return nullptr;
    }

    ManagementUiDescriptor managementUi() const override
    {
        return {QStringLiteral(QUEMUSIC_PLUGIN_UI_API_V1),
                QUrl(QStringLiteral("qml/ManagementPage.qml")), true, true};
    }

    QObject *createManagementBackend(const PluginUiContextData &, QObject *parent) override
    {
        return new PluginUiQrBackend(parent);
    }
};

#include "ExternalPlugin.moc"
