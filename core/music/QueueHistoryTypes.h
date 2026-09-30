#pragma once

#include "sdk/source/v2/SourceV2Types.h"

#include <QDateTime>
#include <QList>
#include <QStringList>
#include <QUuid>

// Host-private persistence values. Source SDK layout and ABI are unchanged.
struct QueueOccurrence {
    QUuid occurrenceId;
    MediaRefV2 ref;
    QString title;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
    bool playableAtEnqueue = false;
};

struct RecentPlay {
    QUuid occurrenceId;
    MediaRefV2 ref;
    QString title;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
    QDateTime playedAt;
};

struct QueueHistorySnapshot {
    QList<QueueOccurrence> queue;
    QList<RecentPlay> history;
    int legacyImportVersion = 0;
};

enum class QueueHistoryDecodeStatus { Ok, Corrupt, UnsupportedVersion, TooLarge };

struct DecodeResult {
    QueueHistoryDecodeStatus status = QueueHistoryDecodeStatus::Corrupt;
    QueueHistorySnapshot snapshot;
};

struct LegacyImportResult {
    QList<QueueOccurrence> accepted;
    int rejected = 0;
    bool parsed = false;
};
