// SPDX-License-Identifier: Apache-2.0
import QtQml

QtObject {
    id: root
    property var musicAdapter: null
    property var controls: null
    property var legacyPlayer: null
    // Sticky until a genuine legacy playback is explicitly started by Host.
    property bool sourceMode: false
    property bool sourceActive: false
    property var legacyLines: []
    property var legacyTranslations: []
    property real legacyPosition: 0
    property real legacyDuration: 0
    property real legacyRate: 1
    property bool legacyPlaying: false
    property bool legacyActive: false
    property string legacyCover: ""
    property var sourceItem: ({})
    property var legacyDetails: ({})
    property var legacyWavePath: []
    // Core visualization is not wired yet; Source must never reuse a legacy frame.
    readonly property var wavePath: sourceMode ? [] : legacyWavePath
    // Display-only projection. Never copy arbitrary metadata, identities or resources.
    readonly property var details: {
        if (sourceMode) {
            var item = sourceActive ? sourceItem || {} : {};
            var artists = item.artists || [];
            return {title: item.title || "", artist: typeof artists.join === "function" ? artists.join(", ") : "",
                    album: item.album || "", sourceLabel: item.sourceLabel || "",
                    fileName: "", date: "", format: ""};
        }
        var legacy = legacyDetails || {};
        return {title: legacy.title || "", artist: legacy.artist || "", album: legacy.album || "", sourceLabel: "",
                fileName: legacy.fileName || "", date: legacy.date || "", format: legacy.format || ""};
    }
    readonly property string defaultCover: "qrc:/QueMusic/resources/app/musicpic.png"
    readonly property string cover: sourceMode
        ? (sourceActive && musicAdapter ? (String(musicAdapter.currentCover || "") || defaultCover) : defaultCover)
        : (legacyCover || defaultCover)

    readonly property var lines: sourceMode ? (sourceActive && musicAdapter ? musicAdapter.currentLyrics || [] : []) : legacyLines
    readonly property var translations: sourceMode ? [] : legacyTranslations
    readonly property string state: sourceMode ? (!sourceActive ? "idle" : musicAdapter ? musicAdapter.currentLyricsState || "empty" : "empty")
                                                  : (legacyLines.length > 0 ? "ready" : "empty")
    readonly property real position: sourceMode ? (sourceActive && controls ? controls.position : 0) : legacyPosition
    readonly property real duration: sourceMode ? (sourceActive && controls ? controls.duration : 0) : legacyDuration
    readonly property real playbackRate: sourceMode ? (controls ? controls.playbackRate : 1) : legacyRate
    readonly property bool playing: sourceMode ? !!(sourceActive && controls && controls.playing) : legacyPlaying
    readonly property bool active: sourceMode ? sourceActive : legacyActive
    readonly property bool seekable: sourceMode ? !!(sourceActive && controls && controls.seekable) : legacyActive
    readonly property var favorite: sourceMode && sourceActive && musicAdapter ? musicAdapter.currentFavorite || {} : ({})
    readonly property bool favoriteEnabled: !!(sourceMode && sourceActive && favorite.token && !favorite.pending
                                               && (favorite.canFavorite || favorite.canUnfavorite))
    readonly property var download: sourceMode && sourceActive && musicAdapter ? musicAdapter.currentDownload || {} : ({})
    readonly property bool downloadEnabled: !!(sourceMode && sourceActive && download.token && download.canDownload && !download.pending)
    readonly property bool rateEnabled: sourceMode
        ? !!(controls && typeof controls.setPlaybackRate === "function") : !!legacyPlayer

    function setPlaybackRate(value) {
        // Preserve the original UI range; never coerce strings or invalid numbers.
        if (typeof value !== "number" || !Number.isFinite(value) || value < 0.1 || value > 4 || !rateEnabled) return false;
        if (sourceMode) controls.setPlaybackRate(value);
        else legacyPlayer.playbackRate = value;
        return true;
    }

    function saveDownload(destination, token) {
        if (!downloadEnabled || !token || token !== download.token || !String(destination || "")
                || !musicAdapter || typeof musicAdapter.downloadCurrent !== "function") return false;
        musicAdapter.downloadCurrent(destination, token);
        return true;
    }

    // Return a choice request, never infer a missing read-state as "not favorite".
    function toggleFavorite() {
        if (!favoriteEnabled) return "disabled";
        if (favorite.state === "favorite" && favorite.canUnfavorite)
            return setFavorite(false, favorite.token) ? "submitted" : "disabled";
        if (favorite.state === "notFavorite" && favorite.canFavorite)
            return setFavorite(true, favorite.token) ? "submitted" : "disabled";
        return "choose";
    }

    function setFavorite(value, token) {
        if (!favoriteEnabled || typeof value !== "boolean" || !token || token !== favorite.token
                || !musicAdapter || typeof musicAdapter.setCurrentFavorite !== "function"
                || !(value ? favorite.canFavorite : favorite.canUnfavorite)) return false;
        musicAdapter.setCurrentFavorite(value, token);
        return true;
    }

    function togglePlayback() {
        if (sourceMode ? playing : legacyPlayer && legacyPlayer.playing) pause(); else play();
    }

    function play() {
        if (sourceMode) {
            if (sourceActive && controls) controls.play();
        } else if (legacyPlayer) legacyPlayer.play();
    }

    function pause() {
        if (sourceMode) {
            if (sourceActive && controls) controls.pause();
        } else if (legacyPlayer) legacyPlayer.pause();
    }

    function seek(position) {
        if (!Number.isFinite(position) || position < 0 || !seekable) return;
        if (sourceMode) controls.seek(position);
        else if (legacyPlayer) legacyPlayer.position = position;
    }

    function retry() {
        if (sourceMode && state === "failed" && musicAdapter
                && typeof musicAdapter.retryCurrentLyrics === "function") musicAdapter.retryCurrentLyrics()
    }
}
