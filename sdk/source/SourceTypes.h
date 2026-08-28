#pragma once

#include <QDateTime>
#include <QFlags>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QUrl>

#include <optional>

enum class SourceCapability : quint64 {
    None = 0,
    Search = 1ull << 0,
    Browse = 1ull << 1,
    StreamAudio = 1ull << 2,
    StreamVideo = 1ull << 3,
    Artwork = 1ull << 4,
    Lyrics = 1ull << 5,
    PlaylistRead = 1ull << 6,
    PlaylistWrite = 1ull << 7,
    Favorites = 1ull << 8,
    Download = 1ull << 9,
    Scrobble = 1ull << 10,
};
Q_DECLARE_FLAGS(SourceCapabilities, SourceCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(SourceCapabilities)

enum class SourceErrorKind {
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

struct SourceError {
    SourceErrorKind kind = SourceErrorKind::Unknown;
    QString message;
    std::optional<int> httpStatus;
};

struct SourceDescriptor {
    QString id;
    QString name;
    QString version;
    QString protocol;
    QString sdkVersion;
    SourceCapabilities capabilities;
};

struct SourceAccount {
    QString sourceId;
    QString accountId;
    QString displayName;
};

struct TrackRef {
    QString sourceId;
    QString nativeId;

    friend bool operator==(const TrackRef &left, const TrackRef &right)
    {
        return left.sourceId == right.sourceId && left.nativeId == right.nativeId;
    }

    friend bool operator!=(const TrackRef &left, const TrackRef &right)
    {
        return !(left == right);
    }
};

struct SearchQuery {
    QString query;
    int limit = 50;
};

struct BrowseQuery {
    QString path;
    QString cursor;
    int limit = 50;
};

struct StreamDescriptor {
    TrackRef track;
    QUrl url;
    QMap<QString, QString> headers;
    QString mimeType;
    QDateTime expiresAt;
    bool video = false;
    bool seekable = true;
};

SourceErrorKind sourceErrorKindFromString(const QString &value);
QJsonObject sourceDescriptorToJson(const SourceDescriptor &descriptor);
SourceDescriptor sourceDescriptorFromJson(const QJsonObject &object);
