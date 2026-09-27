#pragma once

#include <QtPlugin>

class SourceContentEventsV1;

// Optional session extension; independent of the frozen Source SDK v2 ABI.
class ISourceContentEventsProviderV1 {
public:
    virtual ~ISourceContentEventsProviderV1() = default;

    // Borrowed object owned by this session (a QObject descendant). The Host
    // subscribes only while holding the session's plugin lease. Never return an
    // event object owned by another instance or by the plugin itself.
    virtual SourceContentEventsV1 *contentEvents() const = 0;
};

#define QUEMUSIC_SOURCE_CONTENT_EVENTS_PROVIDER_V1_IID \
    "org.quemusic.source.ContentEventsProvider/1.0"
Q_DECLARE_INTERFACE(ISourceContentEventsProviderV1,
                    QUEMUSIC_SOURCE_CONTENT_EVENTS_PROVIDER_V1_IID)
