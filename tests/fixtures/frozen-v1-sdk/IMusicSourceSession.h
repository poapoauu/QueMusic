#pragma once

// Frozen from MusicSourcePlugin/1.0 as published on main at 8ad8e59.
// In particular, fetchArtwork occupies the slot between resolveStream and
// fetchLyrics.

#include "SourceTypes.h"

#include <QJsonValue>
#include <QObject>
#include <QUuid>

class IMusicSourceSession : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IMusicSourceSession() override = default;

    virtual QUuid search(const SearchQuery &query) = 0;
    virtual QUuid browse(const BrowseQuery &query) = 0;
    virtual QUuid resolveStream(const TrackRef &track) = 0;
    virtual QUuid fetchArtwork(const TrackRef &track) = 0;
    virtual QUuid fetchLyrics(const TrackRef &track) = 0;
    virtual void cancel(const QUuid &requestId) = 0;

signals:
    void requestSucceeded(QUuid requestId, QString operation, QJsonValue result);
    void requestFailed(QUuid requestId, SourceError error);
    void authenticationChanged(bool authenticated);
};
