// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors

import QtQml

QtObject {
    property var queueModel
    property var legacyPlayer

    function copyQueueEntry(entry) {
        return {
            name: entry.name,
            path: entry.path,
            songer: entry.songer,
            source: entry.source
        };
    }

    function playQueueEntry(index) {
        if (!queueModel || index < 0 || index >= queueModel.count)
            return;
        legacyPlayer.refreshLegacyMusicPlay();
    }
}
