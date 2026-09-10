// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import "qrc:/QueMusic/components"

Item {
    id: page
    objectName: "categoryPage"
    property var hub: null
    property var playback: null
    property bool pageActive: true

    function activate() { if (pageActive && hub) hub.activatePage(1) }
    function goBack() {
        if (hub && hub.canNavigateBack) return hub.navigateBack()
        return false
    }
    onPageActiveChanged: activate()
    onHubChanged: activate()
    Component.onCompleted: activate()

    Row {
        id: header
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 24; height: 40; spacing: 12
        Label { text: qsTr("分类"); font.pixelSize: 26; font.bold: true }
        Button {
            objectName: "categoryBackAction"
            visible: page.hub ? page.hub.canNavigateBack : false
            text: qsTr("返回")
            onClicked: page.goBack()
        }
        SourceScopeSelector { modelObject: page.hub; width: 240 }
    }
    MusicSectionView {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: header.bottom; anchors.bottom: parent.bottom; anchors.margins: 24
        hub: page.hub
        modelObject: page.hub ? page.hub.category : null
        playback: page.playback
        pageKind: 1
    }
}
