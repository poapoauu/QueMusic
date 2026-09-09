#pragma once
#include "v2/SourceV2Types.h"
#include <QObject>
#include <QUuid>

// Host-private, owner-thread boundary. Never register this type with QML.
// prepare must honor the complete URL/headers or return false; it must not play.
// Implementations must tolerate stop during prepare/play and fence by generation.
class PlaybackSink : public QObject {
public:
    using QObject::QObject;
    ~PlaybackSink() override = default;
    virtual bool prepare(StreamDescriptorV2 stream, QUuid generation) = 0;
    virtual void play(QUuid generation) = 0;
    virtual void stop(QUuid generation) = 0;
};
