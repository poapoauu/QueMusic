#include "KugouSourcePlugin.h"
#include "KugouSourceSession.h"
#include "KugouSourceDefinition.h"

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
