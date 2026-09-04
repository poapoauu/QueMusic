#pragma once

#include "SourceV2Types.h"

#include <QtPlugin>
#include <QUuid>

class IPageProviderV2 {
public:
    virtual ~IPageProviderV2() = default;
    virtual QUuid fetchPage(const PageQueryV2 &query) = 0;
};

class IFavoriteProviderV2 {
public:
    virtual ~IFavoriteProviderV2() = default;
    virtual QUuid setFavorite(const MediaRefV2 &media, bool favorite) = 0;
};

class IPlaybackProviderV2 {
public:
    virtual ~IPlaybackProviderV2() = default;
    virtual QUuid resolveStream(const MediaRefV2 &media) = 0;
    virtual QUuid fetchArtwork(const MediaRefV2 &media) = 0;
    virtual QUuid fetchLyrics(const MediaRefV2 &media) = 0;
};

class IRatingProviderV2 {
public:
    virtual ~IRatingProviderV2() = default;
    virtual QUuid setRating(const MediaRefV2 &media, int rating) = 0;
};

class IScrobbleProviderV2 {
public:
    virtual ~IScrobbleProviderV2() = default;
    virtual QUuid scrobble(const MediaRefV2 &media, qint64 positionMs,
                           bool submission) = 0;
};

class IPlaylistProviderV2 {
public:
    virtual ~IPlaylistProviderV2() = default;
    virtual QUuid createPlaylist(const QString &name,
                                 const QList<MediaRefV2> &tracks) = 0;
    virtual QUuid updatePlaylist(const MediaRefV2 &playlist,
                                 const PlaylistChangeV2 &change) = 0;
    virtual QUuid deletePlaylist(const MediaRefV2 &playlist) = 0;
};

class IDownloadProviderV2 {
public:
    virtual ~IDownloadProviderV2() = default;
    virtual QUuid download(const MediaRefV2 &media, const QUrl &destination) = 0;
};

class IPlayQueueProviderV2 {
public:
    virtual ~IPlayQueueProviderV2() = default;
    virtual QUuid fetchPlayQueue() = 0;
    virtual QUuid savePlayQueue(const QList<MediaRefV2> &items,
                                const MediaRefV2 &current, qint64 positionMs) = 0;
};

class IBookmarkProviderV2 {
public:
    virtual ~IBookmarkProviderV2() = default;
    virtual QUuid fetchBookmarks() = 0;
    virtual QUuid createBookmark(const MediaRefV2 &media, qint64 positionMs,
                                 const QString &comment) = 0;
    virtual QUuid deleteBookmark(const MediaRefV2 &media) = 0;
};

class IPluginSettingsProviderV2 {
public:
    virtual ~IPluginSettingsProviderV2() = default;
    virtual SettingsSchemaV2 settingsSchema() const = 0;
};

class ISettingsActionProviderV2 {
public:
    virtual ~ISettingsActionProviderV2() = default;
    virtual SettingsActionCapabilitiesV2 settingsCapabilities() const = 0;
    virtual QUuid runSettingsAction(const QString &actionId) = 0;
};

#define QUEMUSIC_SETTINGS_ACTION_PROVIDER_V2_IID "org.quemusic.source.SettingsActionProvider/2.0"
Q_DECLARE_INTERFACE(ISettingsActionProviderV2, QUEMUSIC_SETTINGS_ACTION_PROVIDER_V2_IID)

#define QUEMUSIC_PAGE_PROVIDER_V2_IID "org.quemusic.source.PageProvider/2.0"
#define QUEMUSIC_PLAYBACK_PROVIDER_V2_IID "org.quemusic.source.PlaybackProvider/2.0"
#define QUEMUSIC_FAVORITE_PROVIDER_V2_IID "org.quemusic.source.FavoriteProvider/2.0"
#define QUEMUSIC_RATING_PROVIDER_V2_IID "org.quemusic.source.RatingProvider/2.0"
#define QUEMUSIC_SCROBBLE_PROVIDER_V2_IID "org.quemusic.source.ScrobbleProvider/2.0"
#define QUEMUSIC_PLAYLIST_PROVIDER_V2_IID "org.quemusic.source.PlaylistProvider/2.0"
#define QUEMUSIC_DOWNLOAD_PROVIDER_V2_IID "org.quemusic.source.DownloadProvider/2.0"
#define QUEMUSIC_PLAY_QUEUE_PROVIDER_V2_IID "org.quemusic.source.PlayQueueProvider/2.0"
#define QUEMUSIC_BOOKMARK_PROVIDER_V2_IID "org.quemusic.source.BookmarkProvider/2.0"
#define QUEMUSIC_PLUGIN_SETTINGS_PROVIDER_V2_IID "org.quemusic.source.PluginSettingsProvider/2.0"

Q_DECLARE_INTERFACE(IPageProviderV2, QUEMUSIC_PAGE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPlaybackProviderV2, QUEMUSIC_PLAYBACK_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IFavoriteProviderV2, QUEMUSIC_FAVORITE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IRatingProviderV2, QUEMUSIC_RATING_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IScrobbleProviderV2, QUEMUSIC_SCROBBLE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPlaylistProviderV2, QUEMUSIC_PLAYLIST_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IDownloadProviderV2, QUEMUSIC_DOWNLOAD_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPlayQueueProviderV2, QUEMUSIC_PLAY_QUEUE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IBookmarkProviderV2, QUEMUSIC_BOOKMARK_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPluginSettingsProviderV2, QUEMUSIC_PLUGIN_SETTINGS_PROVIDER_V2_IID)
