#pragma once

#include <QFlags>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QUrl>

enum class PlaybackCapability : quint64 {
    None = 0,
    Audio = 1ull << 0,
    Video = 1ull << 1,
    Seek = 1ull << 2,
    Volume = 1ull << 3,
};
Q_DECLARE_FLAGS(PlaybackCapabilities, PlaybackCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(PlaybackCapabilities)

struct PlaybackTrackRef {
    QString sourceId;
    QString nativeId;
    friend bool operator==(const PlaybackTrackRef &left, const PlaybackTrackRef &right)
    {
        return left.sourceId == right.sourceId && left.nativeId == right.nativeId;
    }
};

struct PlaybackStreamDescriptor {
    PlaybackTrackRef track;
    QUrl url;
    QMap<QString, QString> headers;
    QString mimeType;
    QDateTime expiresAt;
    bool video = false;
    bool seekable = true;
};
