// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import 'qrc:/QueMusic/components'

Item {
    id: favouritePage
    property var musicAdapter: null
    property var playbackAdapter: null
    property var historyAdapter: null
    readonly property var historyRows: {
        if (!historyAdapter || typeof historyAdapter.entriesForSource !== "function") return []
        // Reading the presentation property registers its change notification.
        const snapshot = historyAdapter.entries
        const scope = musicAdapter ? musicAdapter.selectedSourceInstanceId : ""
        return historyAdapter.entriesForSource(scope || "").map(function(row) {
            const display = Object.assign({}, row)
            display.artist = row.artist + " · " + Qt.formatDateTime(row.playedAt, "yyyy-MM-dd hh:mm")
                + (row.sourceLabel ? " · " + row.sourceLabel : "")
                + (row.replayable ? "" : " · 无法重播")
            return display
        })
    }

    function replayHistory(index) {
        if (!visible || !history.visible || !historyAdapter
                || typeof historyAdapter.playEntry !== "function"
                || !Number.isInteger(index) || index < 0 || index >= historyRows.length) return false
        const row = historyRows[index]
        return row.replayable === true && !!historyAdapter.playEntry(row.key)
    }
    property int setMode: 0
    property list<int> chooseIndex: []
    property bool changingFavoriteContext: false

    function resetFavoriteContext() {
        changingFavoriteContext = true
        favoriteAdapterDetailWindow.visible = false
        changingFavoriteContext = false
        setMode = 0
        chooseIndex = []
    }
    onMusicAdapterChanged: resetFavoriteContext()
    function browseFavorite(row) {
        const adapter = musicAdapter
        if (!adapter || !row || typeof adapter.browse !== "function") return false
        const scope = adapter.selectedSourceInstanceId
        if (!capabilitiesFor(row).canBrowse || musicAdapter !== adapter
                || adapter.selectedSourceInstanceId !== scope) return false
        if (musicAdapter !== adapter || !adapter.browse(row)
                || musicAdapter !== adapter || adapter.selectedSourceInstanceId !== scope) return false
        favoriteAdapterDetailWindow.opened(row)
        if (musicAdapter !== adapter || adapter.selectedSourceInstanceId !== scope || !favoriteAdapterDetailWindow.visible) return false
        mainContent.contentIndexed(1)
        if (musicAdapter !== adapter || adapter.selectedSourceInstanceId !== scope || !favoriteAdapterDetailWindow.visible) return false
        window.exitIndex = 1
        return true
    }
    Connections {
        target: favouritePage.musicAdapter
        ignoreUnknownSignals: true
        function onSelectedSourceInstanceIdChanged() { favouritePage.resetFavoriteContext() }
        function onCategoryNavigationChanged() {
            if (favoriteAdapterDetailWindow.visible && favouritePage.musicAdapter
                    && favouritePage.musicAdapter.categoryCanNavigateBack === false)
                favouritePage.resetFavoriteContext()
        }
    }

    function capabilitiesFor(row) {
        return musicAdapter && row && typeof musicAdapter.capabilities === "function"
                ? musicAdapter.capabilities(row) : ({})
    }

    function modelFor(name) {
        return musicAdapter ? musicAdapter[name] || null : null
    }
    function rowFor(view, index) {
        return view && view.model && typeof view.model.get === "function" ? view.model.get(index) : null
    }

    function selectedRows(model) {
        var rows = []
        for (var i = 0; i < chooseIndex.length; ++i) {
            var row = model.get(chooseIndex[i])
            if (row) rows.push(row)
        }
        return rows
    }

    function selectedCapabilities(model) {
        return musicAdapter && typeof musicAdapter.capabilities === "function"
                ? musicAdapter.capabilities(selectedRows(model)) : ({})
    }

    function retryCurrentSection() {
        if (!musicAdapter || favouriteChildPage.lastIndex > 2) return
        const view = [songs, lists, singer][favouriteChildPage.lastIndex]
        view.retrySection()
    }

    Component.onCompleted: {
        if (musicAdapter)
            musicAdapter.activatePage(2)
    }

    QPages {
        id: favouriteChildPage
        x: 24
        y: 68
        width: parent.width - 48
        height: parent.height - 68
        pageList: [songs,lists,singer,history]

        // 顶部常驻显示
        Item {
            x: 0
            y: -44
            height: 40
            width: favouriteChildPage
            z: 10
            Text {
                x: 0
                y: 0
                height: 40
                verticalAlignment: Text.AlignVCenter
                text: "收藏内容"
                font.weight: Font.DemiBold
                font.pixelSize: Style.settings.pageTitle
                color: Style.themes.fontColor
            }
        }

        QBlurTapBar {
            objectName: "favoriteTabs"
            x: 0
            y: 12
            z: 5
            model: ["歌曲","歌单","关注歌手","历史记录"]
            tabWidth: 90
            width: 364
            rectXy: Qt.rect(0, 12, width, 40)
            blurSource: favouriteChildPage.pageList[favouriteChildPage.lastIndex]
            onTabChange: (index) => {
                favouriteChildPage.stack(index);
                favouritePage.setMode = 0;
                favouritePage.chooseIndex = [];
            }
        }

        // 右侧操作区
        Row {
            x: parent.width - width
            y: 13
            z: 2
            spacing: 8
            QButton {
                visible: musicAdapter && favouriteChildPage.lastIndex < 2
                height: 38
                text: favouritePage.setMode === 1 ? "取消选择" : "选择"
                iconCharacter: "\uf09f"
                buttonColor: favouritePage.setMode === 1 ? Style.themes.containColor : Style.themes.fullColor
                onClicked: {
                    if(favouritePage.setMode === 1) {
                        favouritePage.setMode = 0;
                        favouritePage.chooseIndex = [];
                    } else {
                        favouritePage.setMode = 1;
                    }
                }
            }
        }

        QListView {
            id: songs
            objectName: "favoriteSongsList"
            width: favouriteChildPage.width + 16
            height: favouriteChildPage.height
            model: favouritePage.modelFor("favoriteSongs")
            clip: true
            topMargin: 72
            selectedIndices: favouritePage.chooseIndex
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText0ForRow: function(index) {
                return favouritePage.capabilitiesFor(favouritePage.rowFor(songs, index)).canEnqueue ? "\uf095" : ""
            }
            toolText1ForRow: function(index) {
                return favouritePage.capabilitiesFor(favouritePage.rowFor(songs, index)).canUnfavorite ? "\uf0c8" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 2

            onClicked: (index) => {
                if (!musicAdapter || !model) return
                if (favouritePage.setMode === 1) {
                    var idx = favouritePage.chooseIndex.indexOf(index);
                    if (idx === -1) {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.concat([index]);
                    } else {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.filter(v => v !== index);
                    }
                } else {
                    var row = model.get(index)
                    if (favouritePage.capabilitiesFor(row).canPlay)
                        musicAdapter.play(row)
                }
            }
            onToolClicked: (index,tool) => {
                if (!musicAdapter || !model) return
                const row = model.get(index)
                const caps = favouritePage.capabilitiesFor(row)
                if (tool === 0 && caps.canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && caps.canUnfavorite)
                    musicAdapter.setFavorite(row, false)
            }
            Text {
                anchors.centerIn: parent
                visible: !songs.model || songs.model.count === 0
                text: "没有收藏的内容？快去收藏一些歌曲吧"
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }
        QListView {
            id: lists
            objectName: "favoritePlaylistsList"
            width: favouriteChildPage.width + 16
            height: favouriteChildPage.height
            model: favouritePage.modelFor("favoriteLists")
            clip: true
            isList: true
            topMargin: 72
            visible: false
            selectedIndices: favouritePage.chooseIndex
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText1ForRow: function(index) {
                return favouritePage.capabilitiesFor(favouritePage.rowFor(lists, index)).canUnfavorite ? "\uf0c8" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 2

            onClicked: (index) => {
                if (!musicAdapter || !model) return
                if (favouritePage.setMode === 1) {
                    var idx = favouritePage.chooseIndex.indexOf(index);
                    if (idx === -1) {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.concat([index]);
                    } else {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.filter(v => v !== index);
                    }
                } else {
                    var row = model.get(index)
                    favouritePage.browseFavorite(row)
                }
            }
            onToolClicked: (index,tool) => {
                if (!musicAdapter || !model || tool !== 1) return
                const row = model.get(index)
                if (favouritePage.capabilitiesFor(row).canUnfavorite)
                    musicAdapter.setFavorite(row, false)
            }
            Text {
                anchors.centerIn: parent
                visible: !lists.model || lists.model.count === 0
                text: "没有收藏的内容？快去收藏一些歌单吧"
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }
        QListView {
            id: singer
            objectName: "favoriteArtistsList"
            visible: false
            width: favouriteChildPage.width + 16
            height: favouriteChildPage.height
            model: favouritePage.modelFor("favoriteArtists")
            clip: true
            topMargin: 72
            isList: true
            showListCount: false
            headerModel: ["歌手", "信息", "", "操作"]
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText1ForRow: function(index) {
                return favouritePage.capabilitiesFor(favouritePage.rowFor(singer, index)).canUnfavorite ? "\uf0c8" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 2
            onClicked: (index) => favouritePage.browseFavorite(favouritePage.rowFor(singer, index))
            onToolClicked: (index, tool) => {
                const row = favouritePage.rowFor(singer, index)
                if (tool === 1 && favouritePage.capabilitiesFor(row).canUnfavorite)
                    musicAdapter.setFavorite(row, false)
            }
            Text {
                objectName: "favoriteArtistsStatus"
                anchors.centerIn: parent
                visible: singer.count === 0
                text: !musicAdapter || musicAdapter.favoriteArtistsState === "unavailable" ? "当前音源不可用"
                    : musicAdapter.favoriteArtistsState === "loading" ? "正在加载关注歌手…"
                    : musicAdapter.favoriteArtistsState === "failed" ? "关注歌手加载失败，请重试"
                    : "当前范围暂无关注歌手"
                textFormat: Text.PlainText
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }
        QListView {
            id: history
            objectName: "favoriteHistoryList"
            visible: false
            width: favouriteChildPage.width + 16
            height: favouriteChildPage.height
            model: favouritePage.historyRows
            clip: true
            topMargin: 72
            isList: true
            showListCount: false
            headerModel: ["标题", "播放记录", "", "操作"]
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText0ForRow: function(index) {
                const row = favouritePage.historyRows[index]
                return row && row.replayable ? "\uf04b" : ""
            }
            hasMore: false
            loadingMore: false
            useLegacyLoadingState: false
            isEnd: count > 0
            onClicked: (index) => favouritePage.replayHistory(index)
            onToolClicked: (index, tool) => {
                if (tool === 0) favouritePage.replayHistory(index)
            }
            Text {
                objectName: "favoriteHistoryStatus"
                anchors.centerIn: parent
                visible: history.count === 0
                text: !historyAdapter ? "历史记录不可用" : "当前范围暂无播放记录"
                textFormat: Text.PlainText
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }

        // 选择模式
        Rectangle {
            id: chooseArea
            x: -24
            y: visible ? favouriteChildPage.height - 60 : favouriteChildPage.height
            width: favouritePage.width
            height: 60
            visible: musicAdapter && favouritePage.setMode !== 0 && favouriteChildPage.lastIndex < 2
            color: Style.themes.sideColor
            Behavior on y { NumberAnimation { duration: 420; easing.type: Easing.OutExpo } }
            Rectangle {
                x: 16
                y: 12
                width: 92
                height: 36
                radius: 20
                color: Style.themes.fullColor
                Text {
                    anchors.centerIn: parent
                    text: "多选模式"
                    color: Style.themes.textColor
                    font.pixelSize: Style.settings.textmain
                }
            }
            Rectangle {
                x: 118
                y: 12
                width: 92
                height: 36
                radius: 20
                color: "transparent"//Style.themes.fullColor
                Text {
                    anchors.centerIn: parent
                    text: "已选择:" + favouritePage.chooseIndex.length + "项"
                    color: Style.themes.textColor
                    font.pixelSize: Style.settings.textmain
                }
            }
            Row {
                y: 12
                x: chooseArea.width - width - 16
                spacing: 8
                QButton {
                    shadowEnabled: false
                    height: 36
                    radius: 20
                    buttonColor: "#fa4642"
                    text: "取消收藏"
                    onClicked: {
                        if (!musicAdapter || favouriteChildPage.lastIndex > 1) return
                        const adapterModel = favouriteChildPage.lastIndex === 1
                            ? favouritePage.modelFor("favoriteLists")
                            : favouritePage.modelFor("favoriteSongs")
                        if (!adapterModel || !favouritePage.selectedCapabilities(adapterModel).canUnfavorite)
                            return
                        const rows = favouritePage.selectedRows(adapterModel)
                        for (let selected = 0; selected < rows.length; ++selected)
                            musicAdapter.setFavorite(rows[selected], false)
                        favouritePage.chooseIndex = []
                    }
                }
                QButton {
                    shadowEnabled: false
                    height: 36
                    radius: 20
                    text: "加入播放列表"
                    onClicked: {
                        if (!musicAdapter || favouriteChildPage.lastIndex !== 0) return
                        const adapterSongs = favouritePage.modelFor("favoriteSongs")
                        if (!adapterSongs || !favouritePage.selectedCapabilities(adapterSongs).canEnqueue)
                            return
                        const rows = favouritePage.selectedRows(adapterSongs)
                        for (let selected = 0; selected < rows.length; ++selected)
                            musicAdapter.enqueue(rows[selected])
                    }
                }
                QButton {
                    shadowEnabled: false
                    width: 92
                    height: 36
                    radius: 20
                    buttonColor: Style.themes.themeColor
                    textColor: Style.themes.primaryColor
                    text: "完成"
                    onClicked: {
                        favouritePage.setMode = 0;
                        favouritePage.chooseIndex = [];
                    }
                }
            }
        }
    }

    Item {
        id: favoriteAdapterDetailWindow
        objectName: "favoriteAdapterDetailWindow"
        z: 20
        anchors.fill: parent
        visible: false
        onVisibleChanged: {
            if (!visible) {
                if (window.exitIndex === 1) window.exitIndex = 0
                if (!favouritePage.changingFavoriteContext && musicAdapter
                        && typeof musicAdapter.closeCategoryBrowse === "function")
                    musicAdapter.closeCategoryBrowse()
            }
        }
        function opened(info) { visible = true }
        Connections {
            target: window
            enabled: favoriteAdapterDetailWindow.visible
            function onExit() {
                if (window.exitIndex <= 1)
                    favoriteAdapterDetailWindow.visible = false
            }
        }
        Rectangle {
            anchors.fill: parent
            color: Style.themes.primaryColor
        }
        QButton {
            objectName: "favoriteDetailBack"
            x: 24; y: 96; width: 96; height: 36
            text: "返回"
            onClicked: {
                if (favoriteAdapterDetailWindow.visible && musicAdapter
                        && musicAdapter.categoryCanNavigateBack === true
                        && typeof musicAdapter.categoryBack === "function") musicAdapter.categoryBack()
            }
        }
        Text {
            objectName: "favoriteDetailTitle"
            x: 136; y: 96; width: parent.width - 160; height: 36
            text: musicAdapter ? musicAdapter.categoryTitle || "" : ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: Style.themes.fontColor
            font.pixelSize: Style.settings.pageTitle
        }
        QListView {
            objectName: "favoriteAdapterDetailList"
            id: favoriteDetailList
            x: 24
            y: 184
            width: favoriteAdapterDetailWindow.width - 32
            height: favoriteAdapterDetailWindow.height - 184
            model: favouritePage.modelFor("categoryItems")
            clip: true
            topMargin: 8
            bottomMargin: 24
            menuModel: []
            toolText0: ""
            toolText1: ""
            sourcePaging: true
            sourceAdapter: favoriteAdapterDetailWindow.visible ? musicAdapter : null
            sourcePageKind: 1
            toolText0ForRow: function(index) {
                return favouritePage.capabilitiesFor(favouritePage.rowFor(favoriteDetailList, index)).canEnqueue ? "\uf095" : ""
            }
            toolText1ForRow: function(index) {
                return favouritePage.capabilitiesFor(favouritePage.rowFor(favoriteDetailList, index)).canFavorite ? "\uf0c8" : ""
            }
            onClicked: (index) => {
                if (!favoriteAdapterDetailWindow.visible || !musicAdapter || !model) return
                var row = model.get(index)
                if (favouritePage.capabilitiesFor(row).canBrowse)
                    favouritePage.browseFavorite(row)
                else if (favouritePage.capabilitiesFor(row).canPlay)
                    musicAdapter.play(row)
            }
            onToolClicked: (index, tool) => {
                if (!favoriteAdapterDetailWindow.visible || !musicAdapter || !model) return
                var row = model.get(index)
                if (tool === 0 && favouritePage.capabilitiesFor(row).canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && favouritePage.capabilitiesFor(row).canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
        }
    }

}
