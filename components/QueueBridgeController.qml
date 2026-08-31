// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQml

QtObject {
    property var queueModel
    property var bridge
    property var legacyPlayer

    function copyQueueEntry(entry) {
        return {
            name: entry.name,
            path: entry.path,
            songer: entry.songer,
            source: entry.source,
            bridge: entry.bridge === true,
            mediaId: entry.mediaId,
            albumTitle: entry.albumTitle,
            artworkUrl: entry.artworkUrl,
            durationMs: entry.durationMs
        }
    }

    function playQueueEntry(index) {
        if (index < 0 || index >= queueModel.count)
            return
        var entry = queueModel.get(index)
        if (entry.bridge === true && entry.mediaId) {
            bridge.play({
                sourceId: entry.mediaId.sourceId,
                accountId: entry.mediaId.accountId,
                nativeId: entry.mediaId.nativeId,
                kind: entry.mediaId.kind,
                title: entry.name,
                subtitle: entry.songer,
                artists: entry.songer ? [entry.songer] : [],
                albumTitle: entry.albumTitle || "",
                artworkUrl: entry.artworkUrl || "",
                durationMs: entry.durationMs || 0
            })
            return
        }
        legacyPlayer.refreshLegacyMusicPlay()
    }
}
