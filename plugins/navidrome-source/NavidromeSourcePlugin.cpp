#include "NavidromeSourcePlugin.h"

#include "NavidromeSourceSession.h"

SourceDescriptorV2 NavidromeSourcePlugin::descriptor() const
{
    SourceDescriptorV2 result;
    result.pluginPackageId = QStringLiteral("org.quemusic.source.navidrome");
    result.sourceId = QStringLiteral("navidrome");
    result.name = QStringLiteral("Navidrome");
    result.version = QStringLiteral("1.0.0");
    result.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    return result;
}

SettingsSchemaV2 NavidromeSourcePlugin::settingsSchema() const
{
    SettingsFieldV2 serverUrl{QStringLiteral("serverUrl"), QStringLiteral("settings.serverUrl"),
                              SettingsFieldTypeV2::Url};
    serverUrl.required = true;
    SettingsFieldV2 username{QStringLiteral("username"), QStringLiteral("settings.username"),
                             SettingsFieldTypeV2::Text};
    username.required = true;
    SettingsFieldV2 password{QStringLiteral("password"), QStringLiteral("settings.password"),
                             SettingsFieldTypeV2::Secret};
    password.required = true;
    password.secret = true;
    SettingsFieldV2 quality{QStringLiteral("quality"), QStringLiteral("settings.quality"),
                            SettingsFieldTypeV2::Choice};
    quality.defaultValue = QStringLiteral("original");
    quality.choices = {QStringLiteral("original"), QStringLiteral("transcoded")};
    return {{QStringLiteral("connection"), QStringLiteral("settings.connection"),
             {serverUrl, username, password, quality}}};
}

IMusicSourceSessionV2 *NavidromeSourcePlugin::createSession(
    const SourceConfigurationV2 &configuration, QObject *parent)
{
    return new NavidromeSourceSession(configuration, &m_network, parent);
}
