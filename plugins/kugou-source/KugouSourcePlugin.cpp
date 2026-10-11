#include "KugouSourcePlugin.h"
#include "KugouSourceSession.h"

SourceDescriptorV2 KugouSourcePlugin::descriptor() const
{
    SourceDescriptorV2 result;
    result.pluginPackageId = "org.quemusic.source.kugou";
    result.sourceId = "kugou";
    result.name = "Kugou (experimental anonymous)";
    result.version = "0.1.0";
    result.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    result.declaredActions.insert(SourceActionV2::Play, {AvailabilityV2::Available, {}, {}});
    return result;
}
SettingsSchemaV2 KugouSourcePlugin::settingsSchema() const
{
    // There are no ordinary settings in the anonymous prototype. Credentials,
    // device identity and QR business must belong to a future Plugin UI API.
    return {};
}
IMusicSourceSessionV2 *KugouSourcePlugin::createSession(
    const SourceConfigurationV2 &configuration, QObject *parent)
{
    return new KugouSourceSession(configuration, parent);
}
