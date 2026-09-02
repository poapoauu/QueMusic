#pragma once

#include "SourceV2Types.h"

#include <QObject>
#include <QUuid>

class IMusicSourceSessionV2 : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IMusicSourceSessionV2() override = default;
    virtual SourceIdentityV2 identity() const = 0;
    virtual SourceSessionStateV2 state() const = 0;
    virtual CapabilitySetV2 capabilities() const = 0;
    virtual QUuid open() = 0;
    virtual void close() = 0;
    virtual void cancel(const QUuid &requestId) = 0;

signals:
    // open() and every asynchronous provider method must emit this before a terminal signal.
    void requestStarted(QUuid requestId);
    void stateChanged(SourceSessionStateV2 state);
    void capabilitiesChanged(CapabilitySetV2 capabilities);
    void pageReady(QUuid requestId, PageResultV2 result);
    void streamReady(QUuid requestId, StreamDescriptorV2 stream);
    void actionCompleted(QUuid requestId, ActionResultV2 result);
    void requestFailed(QUuid requestId, SourceErrorV2 error);
};
