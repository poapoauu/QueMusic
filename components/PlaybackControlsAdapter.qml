// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors

import QtQml

QtObject {
    id: root

    required property var controller

    readonly property real position: controller ? controller.position : 0
    readonly property real duration: controller ? controller.duration : 0
    readonly property bool playing: controller ? controller.playing : false
    readonly property bool seekable: controller ? controller.seekable : false
    readonly property real volume: controller ? controller.volume : 1.0
    readonly property real playbackRate: controller ? controller.playbackRate : 1.0
    readonly property bool muted: controller ? controller.muted : false
    readonly property int state: controller ? controller.state : 0

    signal playbackError(string messageKey)

    function play() {
        if (controller)
            controller.play()
    }

    function pause() {
        if (controller)
            controller.pause()
    }

    function stop() {
        if (controller)
            controller.stop()
    }

    function seek(position) {
        if (controller)
            controller.seek(position)
    }

    function setVolume(volume) {
        if (controller)
            controller.setVolume(volume)
    }

    function setPlaybackRate(rate) {
        if (controller)
            controller.setPlaybackRate(rate)
    }

    function setMuted(muted) {
        if (controller)
            controller.setMuted(muted)
    }

    property Connections controllerConnections: Connections {
        target: root.controller

        function onPlaybackError(messageKey) {
            root.playbackError(messageKey)
        }
    }
}
