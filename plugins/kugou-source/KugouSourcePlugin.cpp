#include "KugouSourcePlugin.h"
#include "KugouSourceSession.h"
#include "KugouSourceDefinition.h"
#include "KugouAuthBackend.h"

SourceDescriptorV2 KugouSourcePlugin::descriptor() const
{
    return KugouSourceDefinition::descriptor();
}
SettingsSchemaV2 KugouSourcePlugin::settingsSchema() const
{
    // The Host retains the full secure schema, but never renders this payload
    // as an ordinary editable setting. Management UI is a separate extension.
    return KugouSourceDefinition::settingsSchema();
}
IMusicSourceSessionV2 *KugouSourcePlugin::createSession(
    const SourceConfigurationV2 &configuration, QObject *parent)
{
    return new KugouSourceSession(configuration, parent);
}
ManagementUiDescriptor KugouSourcePlugin::managementUi() const
{
    return {QStringLiteral("1.0"), QUrl(QStringLiteral("qml/ManagementPage.qml")), true, true};
}
QObject *KugouSourcePlugin::createManagementBackend(const PluginUiContextData &data, QObject *parent)
{
    auto *context = qobject_cast<PluginUiContext *>(parent);
    if (!context || !context->isValid() || data.pluginPackageId != "org.quemusic.source.kugou"
        || data.sourceId != "kugou" || (data.mode != PluginUiMode::Create && data.mode != PluginUiMode::Edit)
        || !KugouQrProtocol::validScope(data.sourceInstanceId, data.accountId)
        || context->pluginPackageId() != data.pluginPackageId || context->sourceId() != data.sourceId
        || context->sourceInstanceId() != data.sourceInstanceId || context->accountId() != data.accountId
        || context->mode() != data.mode
        || !qobject_cast<IPluginUiBackendSecretsV1 *>(context->settings())) return nullptr;
    return new KugouAuthBackend(context, context);
}
