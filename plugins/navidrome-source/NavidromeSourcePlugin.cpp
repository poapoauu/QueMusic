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
    for (SourceActionV2 action : {SourceActionV2::Play, SourceActionV2::Artwork,
                                  SourceActionV2::Lyrics, SourceActionV2::Download,
                                  SourceActionV2::Favorite, SourceActionV2::Unfavorite,
                                  SourceActionV2::Rating, SourceActionV2::Scrobble, SourceActionV2::CreatePlaylist,
                                  SourceActionV2::UpdatePlaylist, SourceActionV2::DeletePlaylist,
                                  SourceActionV2::AddPlaylistTracks, SourceActionV2::RemovePlaylistTracks,
                                  SourceActionV2::FetchPlayQueue, SourceActionV2::SavePlayQueue,
                                  SourceActionV2::FetchBookmarks, SourceActionV2::CreateBookmark,
                                  SourceActionV2::DeleteBookmark})
        result.declaredActions.insert(action,{AvailabilityV2::Available,{},{}});
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
