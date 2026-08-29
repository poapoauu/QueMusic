#pragma once

#include "PlaybackTypes.h"
#include "SourceTypes.h"

#include <QObject>

class IPlaybackEngine : public QObject {
public:
    using QObject::QObject;
    ~IPlaybackEngine() override = default;

    virtual void open(const StreamDescriptor &source) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 position) = 0;
    virtual void setVolume(double volume) = 0;
    virtual PlaybackCapabilities capabilities() const = 0;
};
