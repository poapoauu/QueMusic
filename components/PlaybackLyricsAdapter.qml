// SPDX-License-Identifier: Apache-2.0
import QtQml

QtObject {
    id: root
    property var musicAdapter: null
    property var controls: null
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

    function retry() {
        if (sourceMode && state === "failed" && musicAdapter
                && typeof musicAdapter.retryCurrentLyrics === "function") musicAdapter.retryCurrentLyrics()
    }
}
