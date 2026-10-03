#pragma once

#include "v2/SourceV2Types.h"
#include <QtPlugin>
#include <optional>

// Optional exact-identity lookup for a currently indexed item. This does not
// resolve a stream, scan a library, or confer playback permission by itself.
// Call on the Ready session's owner thread while holding its plugin lease;
// copy all plugin-owned strings before releasing the lease.
class IItemLookupProviderV1 {
public:
    virtual ~IItemLookupProviderV1() = default;
    virtual std::optional<MediaItemV2> lookupItem(const MediaRefV2 &ref) const = 0;
};

#define IItemLookupProviderV1_iid "org.quemusic.source.extensions.ItemLookupProvider/1.0"
Q_DECLARE_INTERFACE(IItemLookupProviderV1, IItemLookupProviderV1_iid)
