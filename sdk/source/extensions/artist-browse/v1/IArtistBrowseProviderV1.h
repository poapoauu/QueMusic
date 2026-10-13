#pragma once

#include "v2/SourceV2Types.h"
#include <QtPlugin>

// Optional, independently versioned extension on a v2 Session. No v2 layout or
// vtable changes. Absent extension means the historical Artist -> Albums path.
enum class ArtistBrowseTargetV1 { Albums, Tracks };
class IArtistBrowseProviderV1 {
public:
    virtual ~IArtistBrowseProviderV1() = default;
    // Exact same-instance Artist ref, Ready session, owner thread, callable
    // plugin lease. Synchronous, no I/O/signals/state mutation. nullopt denies
    // browsing; it does NOT request a fallback. Recheck on every Tracks fetch.
    virtual std::optional<ArtistBrowseTargetV1> artistBrowseTarget(const MediaRefV2 &artist) const = 0;
};
#define QUEMUSIC_ARTIST_BROWSE_PROVIDER_V1_IID "org.quemusic.source.extensions.ArtistBrowseProvider/1.0"
Q_DECLARE_INTERFACE(IArtistBrowseProviderV1, QUEMUSIC_ARTIST_BROWSE_PROVIDER_V1_IID)

inline QString artistBrowseFilterKeyV1() { return QStringLiteral("quemusic.artist-browse.v1"); }
// Reserved Host selector. Do not pass it to an old provider which may ignore
// unknown filters. Host strips only this selector after live extension checks;
// IPageProviderV2 receives Category/Tracks and the ordinary artistId filter.
inline bool isArtistTracksQueryV1(const PageQueryV2 &query)
{
    const auto selector = query.filters.value(artistBrowseFilterKeyV1());
    const auto id = query.filters.value(QStringLiteral("artistId"));
    return query.page == MusicPageKindV2::Category && query.section == PageSectionKindV2::Tracks
        && !query.scope.isAggregate() && query.searchText.isEmpty() && query.filters.size() == 2
        && selector.metaType().id() == QMetaType::QString && selector.toString() == QStringLiteral("tracks")
        && id.metaType().id() == QMetaType::QString && !id.toString().trimmed().isEmpty();
}
