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

    function togglePlayback() {
        if (sourceMode) {
            if (!sourceActive || !controls) return;
            if (controls.playing) controls.pause(); else controls.play();
        } else if (legacyPlayer) {
            if (legacyPlayer.playing) legacyPlayer.pause(); else legacyPlayer.play();
        }
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
