#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

enum class MediaKind {
    Track,
    Album,
    Artist,
    Playlist,
    Directory,
};

struct MediaId {
    QString sourceId;
    QString accountId;
    QString nativeId;
    MediaKind kind = MediaKind::Track;

    friend bool operator==(const MediaId &left, const MediaId &right)
    {
        return left.sourceId == right.sourceId && left.accountId == right.accountId &&
               left.nativeId == right.nativeId && left.kind == right.kind;
    }

    friend bool operator!=(const MediaId &left, const MediaId &right)
    {
        return !(left == right);
    }
};

struct MediaItem {
    MediaId id;
    QString title;
    QString subtitle;
    QStringList artists;
    QString albumTitle;
    qint64 durationMs = 0;
    QUrl artworkUrl;
    bool playable = false;
    bool container = false;
    QVariantMap extra;
};

struct MediaPage {
    QList<MediaItem> items;
    QString nextCursor;
    bool hasMore = false;
};

struct PlaybackEntry {
    MediaId id;
    QUrl streamUrl;
    QMap<QString, QString> headers;
    QDateTime expiresAt;
    QUrl artworkUrl;
    QString lyrics;
};

enum class MediaRequestState {
    Idle,
    Loading,
    Ready,
    Empty,
    Failed,
};

enum class MediaErrorKind {
    Unknown,
    Network,
    Authentication,
    Authorization,
    NotFound,
    RateLimited,
    InvalidRequest,
    Unavailable,
    Unsupported,
};

struct MediaError {
    MediaErrorKind kind = MediaErrorKind::Unknown;
    QString message;
    bool retryable = true;
};

QVariantMap mediaIdToVariantMap(const MediaId &id);
MediaId mediaIdFromVariantMap(const QVariantMap &map);
QVariantMap mediaItemToVariantMap(const MediaItem &item);

Q_DECLARE_METATYPE(MediaErrorKind)
Q_DECLARE_METATYPE(MediaRequestState)
