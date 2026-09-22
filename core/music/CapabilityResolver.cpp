#include "CapabilityResolver.h"

ActionAvailabilityV2 CapabilityResolver::resolve(SourceActionV2 action,
    const ActionAvailabilityV2 &plugin, const ActionAvailabilityV2 &server,
    const ActionAvailabilityV2 &account, const ActionAvailabilityV2 &media) const
{
    Q_UNUSED(action)
    return intersectActionAvailabilityV2({plugin, server, account, media});
}

ActionAvailabilityV2 CapabilityResolver::canAddToPlaylist(const MediaRefV2 &playlist, const MediaRefV2 &track) const
{
    const bool same = !playlist.sourcePluginId.isEmpty() && !playlist.accountId.isEmpty()
        && !playlist.sourceInstanceId.isEmpty() && !playlist.entityId.isEmpty() && !track.entityId.isEmpty()
        && playlist.sourcePluginId == track.sourcePluginId && playlist.accountId == track.accountId
        && playlist.sourceInstanceId == track.sourceInstanceId
        && playlist.entityType == MediaEntityTypeV2::Playlist && track.entityType == MediaEntityTypeV2::Track;
    return {same ? AvailabilityV2::Available : AvailabilityV2::Unsupported,
        same ? QString{} : QStringLiteral("music.playlist.sameSourceOnly"), {{"sameSourceOnly", true}}};
}
