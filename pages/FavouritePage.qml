// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import "qrc:/QueMusic/components"

Item {
    id: page
    objectName: "favoritesPage"
    property var hub: null
    property var playback: null
    property bool pageActive: true

    function activate() { if (pageActive && hub) hub.activatePage(2) }
    onPageActiveChanged: activate()
    onHubChanged: activate()
    Component.onCompleted: activate()

    Row {
        id: header
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 24; height: 40; spacing: 16
        Label { text: qsTr("收藏"); font.pixelSize: 26; font.bold: true }
        SourceScopeSelector { modelObject: page.hub; width: 240 }
    }
    MusicSectionView {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: header.bottom; anchors.bottom: parent.bottom; anchors.margins: 24
        hub: page.hub
        modelObject: page.hub ? page.hub.favorites : null
        playback: page.playback
        pageKind: 2
    }
}
