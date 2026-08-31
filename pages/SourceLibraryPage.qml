// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Controls.Basic
import QueMusic 1.0

Item {
    id: sourceLibrary

    property var accountController: mediaBridge ? mediaBridge.accountController : null
    property var selectedAccount: null
    property bool browsing: false
    property var artworkCache: ({})
    property int artworkRevision: 0
    readonly property var currentResults: browsing ? mediaBridge.browseResults : mediaBridge.searchResults

    function enabledAccounts() {
        var allAccounts = accountController ? accountController.accounts : []
        return allAccounts.filter(function(account) { return account.enabled === true })
    }

    function chooseAccount(index) {
        var list = enabledAccounts()
        selectedAccount = index >= 0 && index < list.length && list[index].enabled === true
            ? list[index] : null
    }

    function ensureSelectedAccount() {
        var list = enabledAccounts()
        var selectedIndex = -1
        if (selectedAccount) {
            for (var i = 0; i < list.length; ++i) {
                if (list[i].accountId === selectedAccount.accountId) {
                    selectedIndex = i
                    break
                }
            }
        }
        if (selectedIndex < 0 && list.length > 0)
            selectedIndex = 0
        sourceSelector.currentIndex = selectedIndex
        chooseAccount(selectedIndex)
    }

    function searchLibrary() {
        if (!selectedAccount || !searchInput.text.trim())
            return
        browsing = false
        mediaBridge.search(selectedAccount.sourceId + "/" + selectedAccount.accountId,
                           searchInput.text.trim(), 50)
    }

    function browseRoot() {
        if (!selectedAccount)
            return
        browsing = true
        mediaBridge.browse(selectedAccount.sourceId, selectedAccount.accountId, "", 4, 50)
    }

    Connections {
        target: accountController
        function onAccountsChanged() {
            sourceLibrary.ensureSelectedAccount()
        }
    }

    Connections {
        target: mediaBridge
        function onArtworkReady(artwork) {
            var id = artwork.mediaId
            if (!id)
                return
            sourceLibrary.artworkCache[id.sourceId + "/" + id.accountId + "/" + id.nativeId] = artwork.artworkUrl
            sourceLibrary.artworkRevision++
        }
        function onMediaActionFailed(error) {
            if (error.action === "play")
                mainWarn.tiped(error.message, 2)
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Row {
            width: parent.width
            height: 44
            spacing: 12

            Text {
                width: 180
                height: parent.height
                text: "来源音乐库"
                verticalAlignment: Text.AlignVCenter
                color: Style.themes.fontColor
                font.pixelSize: Style.settings.pageTitle
                font.weight: Font.DemiBold
            }

            ComboBox {
                id: sourceSelector
                width: 220
                height: 38
                anchors.verticalCenter: parent.verticalCenter
                model: sourceLibrary.enabledAccounts()
                textRole: "displayName"
                enabled: model.length > 0
                onCurrentIndexChanged: sourceLibrary.chooseAccount(currentIndex)
                Component.onCompleted: sourceLibrary.ensureSelectedAccount()
            }

            Text {
                visible: accountController && accountController.accounts.length > sourceLibrary.enabledAccounts().length
                height: parent.height
                text: "已禁用账户无法使用"
                verticalAlignment: Text.AlignVCenter
                color: Style.themes.textColor
                font.pixelSize: Style.settings.textTip
            }

            Button {
                text: "浏览库"
                height: 38
                enabled: sourceLibrary.selectedAccount !== null
                onClicked: sourceLibrary.browseRoot()
            }

            Text {
                visible: accountController && accountController.lastError.length > 0
                width: parent.width - 520
                height: parent.height
                text: accountController ? accountController.lastError : ""
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                color: "#d85a5a"
            }
        }

        Row {
            width: parent.width
            height: 42
            spacing: 8

            TextField {
                id: searchInput
                width: parent.width - searchButton.width - 8
                height: parent.height
                placeholderText: "在选定来源中搜索歌曲、专辑或歌单"
                enabled: sourceLibrary.selectedAccount !== null
                onAccepted: sourceLibrary.searchLibrary()
            }
            Button {
                id: searchButton
                width: 88
                height: parent.height
                text: "搜索"
                enabled: sourceLibrary.selectedAccount !== null && searchInput.text.trim().length > 0
                onClicked: sourceLibrary.searchLibrary()
            }
        }

        Rectangle {
            width: parent.width
            height: parent.height - 118
            color: Style.themes.primaryColor
            radius: Style.settings.cubeRadius
            clip: true

            Text {
                anchors.centerIn: parent
                visible: !sourceLibrary.selectedAccount
                text: "请先在设置中添加并启用一个 Navidrome 音乐源。"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.textmain
            }

            Text {
                anchors.centerIn: parent
                visible: sourceLibrary.selectedAccount && sourceLibrary.currentResults.requestState === 1
                text: "正在加载…"
                color: Style.themes.textColor
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 420)
                spacing: 12
                visible: sourceLibrary.selectedAccount && sourceLibrary.currentResults.requestState === 4
                Text {
                    width: parent.width
                    text: sourceLibrary.currentResults.errorMessage
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: "#d85a5a"
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "重试"
                    visible: sourceLibrary.currentResults.canRetry
                    onClicked: mediaBridge.retry()
                }
            }

            Text {
                anchors.centerIn: parent
                visible: sourceLibrary.selectedAccount && sourceLibrary.currentResults.requestState === 3
                text: sourceLibrary.browsing ? "这个位置没有内容。" : "没有找到匹配的内容。"
                color: Style.themes.textColor
            }

            ListView {
                id: resultView
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                spacing: 6
                visible: sourceLibrary.selectedAccount && sourceLibrary.currentResults.count > 0
                model: sourceLibrary.currentResults

                delegate: Rectangle {
                    id: resultDelegate
                    required property int index
                    width: resultView.width
                    height: 64
                    radius: Style.settings.labelRadius
                    color: itemMouse.containsMouse ? Style.themes.hoverColor : "transparent"
                    property var item: sourceLibrary.currentResults.get(index)
                    property string artworkKey: item.sourceId + "/" + item.accountId + "/" + item.nativeId

                    Component.onCompleted: {
                        if (!item.artworkUrl)
                            mediaBridge.loadArtwork(item)
                    }

                    Image {
                        width: 44
                        height: 44
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        fillMode: Image.PreserveAspectCrop
                        source: {
                            sourceLibrary.artworkRevision
                            return sourceLibrary.artworkCache[resultDelegate.artworkKey]
                                || resultDelegate.item.artworkUrl
                                || "qrc:/QueMusic/resources/app/musicpic.png"
                        }
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 66
                        anchors.right: actionButton.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            width: parent.width
                            text: resultDelegate.item.title
                            elide: Text.ElideRight
                            color: Style.themes.fontColor
                            font.pixelSize: Style.settings.textmain
                        }
                        Text {
                            width: parent.width
                            text: resultDelegate.item.subtitle || resultDelegate.item.albumTitle
                            elide: Text.ElideRight
                            color: Style.themes.textColor
                            font.pixelSize: Style.settings.text
                        }
                    }
                    Button {
                        id: actionButton
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: resultDelegate.item.container ? "打开" : "播放"
                        onClicked: {
                            if (resultDelegate.item.container) {
                                sourceLibrary.browsing = true
                                mediaBridge.open(resultDelegate.item)
                            } else {
                                mediaBridge.play(resultDelegate.item)
                            }
                        }
                    }
                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }

                footer: Button {
                    width: resultView.width
                    height: 42
                    visible: sourceLibrary.currentResults.hasMore
                    text: "加载更多"
                    onClicked: mediaBridge.loadMore()
                }
            }
        }
    }
}
