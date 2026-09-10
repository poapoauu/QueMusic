// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import "qrc:/QueMusic/components"

Item {
    id: page
    objectName: "searchPage"
    property var hub: null
    property var playback: null
    property bool pageActive: true

    function activate() { if (pageActive && hub) hub.activatePage(3) }
    function submitSearch(text) {
        var query = String(text === undefined ? searchInput.text : text).trim()
        if (!hub || query.length === 0) return false
        hub.search(query)
        return true
    }
    onPageActiveChanged: activate()
    onHubChanged: activate()
    Component.onCompleted: activate()

    Row {
        id: header
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 24; height: 40; spacing: 12
        Label { text: qsTr("搜索"); font.pixelSize: 26; font.bold: true }
        TextField {
            id: searchInput
            objectName: "musicHubSearchInput"
            width: Math.max(180, page.width - 560)
            placeholderText: qsTr("搜索歌曲、专辑、歌手或歌单")
            onAccepted: page.submitSearch(text)
        }
        Button { text: qsTr("搜索"); onClicked: page.submitSearch(searchInput.text) }
        SourceScopeSelector { modelObject: page.hub; width: 220 }
    }
    MusicSectionView {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: header.bottom; anchors.bottom: parent.bottom; anchors.margins: 24
        hub: page.hub
        modelObject: page.hub ? page.hub.searchResults : null
        playback: page.playback
        pageKind: 3
    }
}
