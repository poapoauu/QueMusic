#pragma once

#include "SourceTypes.h"

#include <QtPlugin>
#include <QUuid>

class IMusicSourceArtworkSession {
public:
    virtual ~IMusicSourceArtworkSession() = default;
    virtual QUuid fetchArtwork(const TrackRef &track) = 0;
};

#define QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID \
    "org.quemusic.MusicSourceArtworkSession/1.0"

Q_DECLARE_INTERFACE(IMusicSourceArtworkSession,
                    QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID)
