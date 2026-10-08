#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

enum class AvailabilityV2 { Unsupported, Available, Unavailable, Forbidden };
enum class MediaEntityTypeV2 { Track, Album, Artist, Playlist, Genre, Directory };
enum class MusicPageKindV2 { Recommendation, Category, Favorites, Search };
enum class SourceSessionStateV2 {
    Closed, Connecting, Ready, AuthenticationRequired, Failed, Closing
};
enum class SourceErrorKindV2 {
    Unknown, Network, Authentication, Authorization, NotFound, RateLimited,
    InvalidRequest, Unavailable, Unsupported
};
enum class PageLoadStateV2 { Idle, Loading, Ready, Empty, Failed };
enum class SourcePageLoadStateV2 { Loading, Ready, Empty, Failed };
enum class SettingsFieldTypeV2 { Text, Secret, Url, Integer, Boolean, Choice, Directory };
enum class SourceActionV2 {
    Play, Artwork, Lyrics, Download, Favorite, Unfavorite, Rating, Scrobble,
    CreatePlaylist, UpdatePlaylist, DeletePlaylist, AddPlaylistTracks,
    RemovePlaylistTracks, FetchPlayQueue, SavePlayQueue,
    FetchBookmarks, CreateBookmark, DeleteBookmark
};
enum class PageSectionKindV2 {
    RecentlyPlayed, FrequentlyPlayed, HighestRated, Newest, Random,
    Genres, Artists, Albums, Tracks, FavoriteTracks, FavoriteAlbums,
    FavoriteArtists, Playlists, SearchResults
};

struct MediaRefV2 {
    // Legacy field name: SourceDescriptorV2::sourceId, not the plugin package ID.
    QString sourcePluginId;
    QString sourceInstanceId;
    QString accountId;
    MediaEntityTypeV2 entityType = MediaEntityTypeV2::Track;
    QString entityId;
    friend bool operator==(const MediaRefV2 &left, const MediaRefV2 &right)
    {
        return left.sourcePluginId == right.sourcePluginId
            && left.sourceInstanceId == right.sourceInstanceId
            && left.accountId == right.accountId
            && left.entityType == right.entityType
            && left.entityId == right.entityId;
    }
    friend bool operator!=(const MediaRefV2 &left, const MediaRefV2 &right)
    {
        return !(left == right);
    }
};

struct ActionAvailabilityV2 {
    AvailabilityV2 state = AvailabilityV2::Unsupported;
    QString reasonKey;
    QVariantMap constraints;
    friend bool operator==(const ActionAvailabilityV2 &left,
                           const ActionAvailabilityV2 &right)
    {
        return left.state == right.state && left.reasonKey == right.reasonKey
            && left.constraints == right.constraints;
    }
};

struct SourceIdentityV2 {
    // Legacy field name: SourceDescriptorV2::sourceId, not the plugin package ID.
    QString sourcePluginId;
    QString sourceInstanceId;
    QString accountId;
    QString displayName;
};

struct SourceDescriptorV2 {
    QString pluginPackageId;
    QString sourceId;
    QString name;
    QString version;
    int sdkAbi = 2;
    QHash<SourceActionV2, ActionAvailabilityV2> declaredActions;
};

// Layers are ordered from least to most object-specific. Unknown constraints
// fail closed; only sameSourceOnly (bool) and maxBitrate (finite >= 0) are understood.
ActionAvailabilityV2 intersectActionAvailabilityV2(const QList<ActionAvailabilityV2> &layers);

struct CapabilitySetV2 {
    QHash<SourceActionV2, ActionAvailabilityV2> serverActions;
    QHash<SourceActionV2, ActionAvailabilityV2> accountActions;
    ActionAvailabilityV2 serverAction(SourceActionV2 key) const;
    ActionAvailabilityV2 accountAction(SourceActionV2 key) const;
    ActionAvailabilityV2 action(SourceActionV2 key) const;
};

struct SourceScopeV2 {
    QString sourceInstanceId; // empty means Aggregate
    bool isAggregate() const { return sourceInstanceId.isEmpty(); }
};

struct PageQueryV2 {
    MusicPageKindV2 page = MusicPageKindV2::Recommendation;
    PageSectionKindV2 section = PageSectionKindV2::RecentlyPlayed;
    SourceScopeV2 scope;
    QString searchText;
    QVariantMap filters;
    QString cursor;
    int limit = 50;
};

struct MediaItemV2 {
    MediaRefV2 ref;
    QString title;
    QString subtitle;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
    QString artworkId;
    QVariantMap externalIds;
    // Optional collectionKind="chart" classifies a Playlist as a source-owned
    // chart. It uses the ordinary playlist browse/play capabilities and refs.
    QVariantMap metadata;
    QHash<SourceActionV2, ActionAvailabilityV2> availableActions;
};

struct PageSectionV2 {
    QString sectionId;
    QString titleKey;
    PageSectionKindV2 kind = PageSectionKindV2::Tracks;
    QString layoutHint;
    QList<MediaItemV2> items;
    QString nextCursor;
    bool hasMore = false;
};

struct SourceErrorV2 {
    SourceErrorKindV2 kind = SourceErrorKindV2::Unknown;
    QString messageKey;
    QString detail;
    std::optional<int> httpStatus;
    bool retryable = true;
};

struct SourcePageStateV2 {
    SourcePageLoadStateV2 state = SourcePageLoadStateV2::Loading;
    std::optional<SourceErrorV2> error;
};

struct PageResultV2 {
    QList<PageSectionV2> sections;
    QHash<QString, SourcePageStateV2> sourceStates;
    bool cached = false;
    bool complete = true;
};

struct SourcePageResultV2 {
    QString sourceInstanceId;
    PageResultV2 page;
    std::optional<SourceErrorV2> error;
};

struct PlaylistChangeV2 {
    QList<MediaRefV2> tracksToAdd;
    QList<int> trackIndexesToRemove;
    QString newName;
};

struct ActionResultV2 {
    SourceActionV2 action = SourceActionV2::Play;
    MediaRefV2 subject;
    QVariantMap payload;
};

struct SourceConfigurationV2 {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    QString displayName;
    QVariantMap parameters;
    QByteArray secret;
};

struct StreamDescriptorV2 {
    MediaRefV2 media;
    QUrl url;
    QMap<QString, QString> headers;
    QString mimeType;
    QDateTime expiresAt;
    bool seekable = true;
};

enum class SettingsComparisonV2 { Equal, NotEqual };
struct SettingsVisibilityConditionV2 {
    QString fieldId;
    SettingsComparisonV2 comparison = SettingsComparisonV2::Equal;
    QVariant value;
};
struct SettingsActionDescriptorV2 {
    QString id;
    QString labelKey;
    bool requiresConfirmation = false;
};
struct SettingsActionCapabilitiesV2 {
    QHash<QString, ActionAvailabilityV2> serverActions;
    QHash<QString, ActionAvailabilityV2> accountActions;
};
struct SettingsFieldV2 {
    QString id;
    QString labelKey;
    SettingsFieldTypeV2 type = SettingsFieldTypeV2::Text;
    bool required = false;
    bool secret = false;
    QVariant defaultValue;
    QVariantList choices;
    QVariantMap constraints;
    std::optional<SettingsVisibilityConditionV2> visibleWhen;
};

struct SettingsSectionV2 {
    QString id;
    QString titleKey;
    QList<SettingsFieldV2> fields;
    QList<SettingsActionDescriptorV2> actions;
};
using SettingsSchemaV2 = QList<SettingsSectionV2>;

QVariantMap mediaRefV2ToVariantMap(const MediaRefV2 &media);
MediaRefV2 mediaRefV2FromVariantMap(const QVariantMap &map);
QJsonObject actionAvailabilityV2ToJson(const ActionAvailabilityV2 &availability);
ActionAvailabilityV2 actionAvailabilityV2FromJson(const QJsonObject &object);

Q_DECLARE_METATYPE(SourceSessionStateV2)
Q_DECLARE_METATYPE(CapabilitySetV2)
Q_DECLARE_METATYPE(PageResultV2)
Q_DECLARE_METATYPE(StreamDescriptorV2)
Q_DECLARE_METATYPE(ActionResultV2)
Q_DECLARE_METATYPE(SourceErrorV2)
