#pragma once

#include "v2/SourceV2Types.h"

enum class SmokeCleanupAction { None, DeletePlaylist, RestoreFavorite };

class NavidromeSmokeState {
public:
    void recordInitialFavorite(bool favorite) { m_initialFavorite = favorite; }
    void recordFavoriteMutation() { m_favoriteNeedsRestore = true; }
    void recordPlaylistCreated(const MediaRefV2 &playlist) { m_playlist = playlist; }
    void recordPlaylistDeleted() { m_playlist = {}; }
    void recordFavoriteRestored() { m_favoriteNeedsRestore = false; }
    void recordCleanupFailure(SmokeCleanupAction action)
    {
        if (action == SmokeCleanupAction::DeletePlaylist)
            m_playlist = {};
        else if (action == SmokeCleanupAction::RestoreFavorite)
            m_favoriteNeedsRestore = false;
    }

    SmokeCleanupAction nextCleanupAction() const
    {
        if (!m_playlist.entityId.isEmpty())
            return SmokeCleanupAction::DeletePlaylist;
        if (m_favoriteNeedsRestore)
            return SmokeCleanupAction::RestoreFavorite;
        return SmokeCleanupAction::None;
    }
    bool initialFavorite() const { return m_initialFavorite; }
    MediaRefV2 playlist() const { return m_playlist; }

private:
    bool m_initialFavorite = false;
    bool m_favoriteNeedsRestore = false;
    MediaRefV2 m_playlist;
};
