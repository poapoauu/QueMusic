// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors

import QtQml

QtObject {
    property var queueModel
    property var secureQueueModel
    property var legacyPlayer
    property var playbackCoordinator
    property bool useCoordinator: false

    function copyQueueEntry(entry) {
        return {
            name: entry.name,
            path: entry.path,
            songer: entry.songer,
            source: entry.source
        };
    }

    function playQueueEntry(index) {
        if (!Number.isInteger(index) || (useCoordinator && !playbackCoordinator)) return;
        // A Source intent never borrows a Legacy queue or player when its
        // dependencies disappear. Explicit Legacy mode remains independent.
        const activeQueue = useCoordinator ? secureQueueModel : queueModel;
        const count = activeQueue && activeQueue.count !== undefined
            ? activeQueue.count : (activeQueue ? activeQueue.length : 0);
        if (!activeQueue || !Number.isInteger(count) || index < 0 || index >= count)
            return;
        if (useCoordinator) {
            if (typeof playbackCoordinator.playQueueEntry === "function")
                playbackCoordinator.playQueueEntry(index);
            return;
        }
        if (legacyPlayer && typeof legacyPlayer.refreshLegacyMusicPlay === "function")
            legacyPlayer.refreshLegacyMusicPlay();
    }
}
