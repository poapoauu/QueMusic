#pragma once

#include "v2/SourceV2Types.h"
#include <QtPlugin>

// Independently versioned, optional extension on an IMusicSourceSessionV2.
// These are account-personalized feeds, never aliases for Random or Newest.
enum class DiscoveryKindV1 { PersonalRadio, PersonalRadar };

inline QString discoveryFilterKeyV1() { return QStringLiteral("quemusic.discovery.v1"); }

inline PageQueryV2 discoveryQueryV1(DiscoveryKindV1 kind, SourceScopeV2 scope = {}, int limit = 50)
{
    PageQueryV2 query;
    query.section = PageSectionKindV2::Tracks;
    query.scope = scope;
    query.limit = limit;
    query.filters.insert(discoveryFilterKeyV1(), kind == DiscoveryKindV1::PersonalRadio
        ? QStringLiteral("personal-radio") : kind == DiscoveryKindV1::PersonalRadar
        ? QStringLiteral("personal-radar") : QString{});
    return query;
}

// Reserved Host routing selector. Reject malformed/combined queries rather than
// forwarding an unknown filter to an old IPageProviderV2 that might ignore it.
inline std::optional<DiscoveryKindV1> discoveryKindV1(const PageQueryV2 &query)
{
    const auto value = query.filters.value(discoveryFilterKeyV1());
    if (query.page != MusicPageKindV2::Recommendation
        || query.section != PageSectionKindV2::Tracks || !query.searchText.isEmpty()
        || query.filters.size() != 1 || value.metaType().id() != QMetaType::QString)
        return {};
    if (value.toString() == QStringLiteral("personal-radio")) return DiscoveryKindV1::PersonalRadio;
    if (value.toString() == QStringLiteral("personal-radar")) return DiscoveryKindV1::PersonalRadar;
    return {};
}

class IDiscoveryProviderV1 {
public:
    virtual ~IDiscoveryProviderV1() = default;
    // Per-instance/account state; recheck before every fetch, including cursors.
    // Unsupported means no such service; Unavailable is temporary; Forbidden
    // means this account cannot use it. Notify via v2 capabilitiesChanged when
    // the effective discovery state changes (even if v2 actions are unchanged).
    virtual AvailabilityV2 discoveryAvailability(DiscoveryKindV1 kind) const = 0;
    // Ready session, owner thread, callable plugin lease held by Host. The Host
    // removes its reserved selector: query.filters is empty. scope is concrete,
    // page=Recommendation, section=Tracks, cursor is opaque and provider-owned.
    // Use v2 requestStarted/pageReady/requestFailed and cancel(id) unchanged.
    // Return one complete, non-cached Tracks section containing Track refs from
    // this session only. Empty succeeds; hasMore requires a nonempty nextCursor.
    // This resolves catalog items, NEVER starts playback or returns stream URLs.
    virtual QUuid fetchDiscovery(DiscoveryKindV1 kind, const PageQueryV2 &query) = 0;
};

#define QUEMUSIC_DISCOVERY_PROVIDER_V1_IID "org.quemusic.source.extensions.DiscoveryProvider/1.0"
Q_DECLARE_INTERFACE(IDiscoveryProviderV1, QUEMUSIC_DISCOVERY_PROVIDER_V1_IID)
