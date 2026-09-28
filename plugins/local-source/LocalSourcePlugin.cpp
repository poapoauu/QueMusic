#include "LocalSourcePlugin.h"
#include "LocalSourceSession.h"

SourceDescriptorV2 LocalSourcePlugin::descriptor() const
{
    SourceDescriptorV2 result;
    result.pluginPackageId = QStringLiteral("org.quemusic.source.local");
    result.sourceId = QStringLiteral("local");
    result.name = QStringLiteral("Local Music");
    result.version = QStringLiteral("1.0.0");
    result.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    for (const auto action : {SourceActionV2::Play, SourceActionV2::Artwork,
                              SourceActionV2::Lyrics})
        result.declaredActions.insert(action, {AvailabilityV2::Available, {}, {}});
    return result;
}

SettingsSchemaV2 LocalSourcePlugin::settingsSchema() const
{
    SettingsFieldV2 root{QStringLiteral("rootDirectory"),
                         QStringLiteral("local.settings.rootDirectory"),
                         SettingsFieldTypeV2::Directory};
    root.required = true;
    SettingsFieldV2 recursive{QStringLiteral("recursive"),
                              QStringLiteral("local.settings.recursive"),
                              SettingsFieldTypeV2::Boolean};
    recursive.defaultValue = true;
    SettingsFieldV2 onOpen{QStringLiteral("scanOnOpen"),
                           QStringLiteral("local.settings.scanOnOpen"),
                           SettingsFieldTypeV2::Boolean};
    onOpen.defaultValue = true;
    SettingsFieldV2 watch{QStringLiteral("watchChanges"),
                          QStringLiteral("local.settings.watchChanges"),
                          SettingsFieldTypeV2::Boolean};
    watch.defaultValue = false;
    SettingsFieldV2 ignored{QStringLiteral("ignoreDirectories"),
                            QStringLiteral("local.settings.ignoreDirectories"),
                            SettingsFieldTypeV2::Text};
    ignored.defaultValue = QString();
    SettingsSectionV2 section;
    section.id = QStringLiteral("library");
    section.titleKey = QStringLiteral("local.settings.library");
    section.fields = {root, recursive, onOpen, watch, ignored};
    section.actions = {{QStringLiteral("rescan"), QStringLiteral("local.settings.rescan"), false}};
    return {section};
}
IMusicSourceSessionV2 *LocalSourcePlugin::createSession(
    const SourceConfigurationV2 &configuration, QObject *parent)
{
    return new LocalSourceSession(configuration, &m_pool, parent);
}
