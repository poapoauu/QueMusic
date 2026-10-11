#include "KugouSourcePlugin.h"
#include "KugouSourceSession.h"
#include "KugouSourceDefinition.h"

SourceDescriptorV2 KugouSourcePlugin::descriptor() const
{
    return KugouSourceDefinition::descriptor();
}
SettingsSchemaV2 KugouSourcePlugin::settingsSchema() const
{
    // There are no ordinary settings in the anonymous prototype. Credentials,
    // device identity and QR business must belong to a future Plugin UI API.
    return KugouSourceDefinition::settingsSchema();
}
IMusicSourceSessionV2 *KugouSourcePlugin::createSession(
    const SourceConfigurationV2 &configuration, QObject *parent)
{
    return new KugouSourceSession(configuration, parent);
}
