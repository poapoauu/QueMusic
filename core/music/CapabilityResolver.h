#pragma once
#include "v2/SourceV2Types.h"

class CapabilityResolver final {
public:
    ActionAvailabilityV2 resolve(SourceActionV2 action,
        const ActionAvailabilityV2 &plugin, const ActionAvailabilityV2 &server,
        const ActionAvailabilityV2 &account, const ActionAvailabilityV2 &media) const;
    ActionAvailabilityV2 canAddToPlaylist(const MediaRefV2 &playlist, const MediaRefV2 &track) const;
};
