// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQml
import QueMusic 1.0

QtObject {
    id: wiring
    property var queueModel
    property var bridge
    property var legacyPlayer

    property var controller: QueueBridgeController {
        queueModel: wiring.queueModel
        bridge: wiring.bridge
        legacyPlayer: wiring.legacyPlayer
    }

    function copyQueueEntry(entry) { return controller.copyQueueEntry(entry) }
    function playQueueEntry(index) { controller.playQueueEntry(index) }
}
