#pragma once

#include "v2/SourceV2Types.h"
#include <QUrl>
#include <QtPlugin>
#include <optional>

// Optional migration-only API. A claim never grants file access and never
// supplies a stream URL. Callers must still route the returned ref through
// PlaybackCoordinator and resolve ambiguities across SourceInstances.
// Query one Ready session on its owner thread while holding its plugin lease.
// The caller must own copies of returned strings before releasing that lease.
// A provider may only claim already indexed media in that instance's configured
// scope. No match (including no completed scan) returns nullopt; it must never
// trigger a scan, create an instance, mutate configuration or start playback.
class ILegacyMediaIdentityProviderV1 {
public:
    virtual ~ILegacyMediaIdentityProviderV1() = default;
    virtual std::optional<MediaRefV2> claimLegacyFile(const QUrl &fileUrl) const = 0;
};

#define ILegacyMediaIdentityProviderV1_iid "org.quemusic.source.extensions.LegacyMediaIdentityProvider/1.0"
Q_DECLARE_INTERFACE(ILegacyMediaIdentityProviderV1, ILegacyMediaIdentityProviderV1_iid)
