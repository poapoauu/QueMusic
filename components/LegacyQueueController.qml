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
        const activeQueue = useCoordinator && playbackCoordinator
            ? (secureQueueModel || queueModel) : queueModel;
        const count = activeQueue && activeQueue.count !== undefined
            ? activeQueue.count : (activeQueue ? activeQueue.length : 0);
        if (!activeQueue || index < 0 || index >= count)
            return;
        if (useCoordinator && playbackCoordinator) {
            playbackCoordinator.playQueueEntry(index);
            return;
        }
        legacyPlayer.refreshLegacyMusicPlay();
    }
}
