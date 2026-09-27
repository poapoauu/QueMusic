#pragma once

#include "extensions/content-events/v1/SourceContentEventsV1Export.h"
#include "v2/SourceV2Types.h"

#include <QObject>

// A stable event emitter shared by native plugins and the Host. This is not a
// new base class or signal on IMusicSourceSessionV2.
class QUEMUSIC_SOURCE_CONTENT_EVENTS_EXPORT SourceContentEventsV1 : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~SourceContentEventsV1() override;

signals:
    // Emit after publishing an immutable snapshot. Revisions increase within
    // one session/configuration generation; old sessions are independently
    // fenced by the Host. No capability change is implied.
    void contentChanged(quint64 revision);

    // Background refresh failures are not terminal signals for a request.
    // messageKey must be stable; do not put private file paths in detail.
    void refreshFailed(SourceErrorV2 error);
};
