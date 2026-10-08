// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQuick
import 'qrc:/QueMusic/components'

Item {
    id: searchPage
    property var musicAdapter: null
    property var playbackAdapter: null
    property int searchTab: 0

    function sourceOptions() {
        return musicAdapter ? musicAdapter.sourceOptions : []
    }
    function sourceChoice() {
        if (!musicAdapter) return -1
        const options = sourceOptions()
        for (let i = 0; i < options.length; ++i) {
            if (options[i].sourceInstanceId === musicAdapter.selectedSourceInstanceId)
                return i
        }
        return -1
    }
    function selectSource(choice) {
        if (!musicAdapter) return
        const option = sourceOptions()[choice]
        if (option && option.available)
            musicAdapter.selectedSourceInstanceId = option.sourceInstanceId
    }
    function capabilitiesFor(row) {
        return musicAdapter && row ? musicAdapter.capabilities(row) : ({})
    }
    function modelFor(name) {
        return musicAdapter ? musicAdapter[name] || null : null
    }
    function rowFor(view, index) {
        return view && view.model && typeof view.model.get === "function" ? view.model.get(index) : null
    }
    function currentModel() {
        switch (searchTab) {
        case 1: return modelFor("searchLists")
        case 2: return modelFor("searchAlbums")
        case 3: return modelFor("searchLyrics")
        default: return modelFor("searchSongs")
        }
    }
    function retryCurrentSection() {
        const views = [searchSong, searchLists, searchAlbum, searchLyrics]
        if (views[searchTab]) views[searchTab].retrySection()
    }
    function browseResult(row) {
        if (!musicAdapter || !capabilitiesFor(row).canBrowse || !musicAdapter.browse(row))
            return
        searchAdapterDetailWindow.opened(row)
        mainContent.contentIndexed(1)
        window.exitIndex = 1
    }
    Component.onCompleted: {
        if (musicAdapter)
            musicAdapter.activatePage(3)
    }

    QPages {
        id: searchChildPage
        x: 24
        y: 68
        width: parent.width - 48
        height: parent.height - 68
        pageList: [searchSong, searchLists, searchAlbum, searchLyrics]

        Item {
            x: 0
            y: -44
            height: 40
            width: parent.width
            z: 10
            Text {
                x: 0
                y: 0
                height: 40
                verticalAlignment: Text.AlignVCenter
                text: "搜索结果"
                font.pixelSize: Style.settings.pageTitle
                font.weight: Font.DemiBold
                color: Style.themes.fontColor
                QLoadSign {
                    id: searchLoad
                    x: parent.width
                    y: 2
                }
            }
            QDrop {
                id: searchSourceScope
                objectName: "searchSourceScope"
                y: 0
                height: 36
                width: 120
                anchors.right: parent.right
                choice: searchPage.sourceChoice()
                textColor: Style.themes.textColor
                color: Style.themes.primaryColor
                border.color: Style.themes.borderColor
                radius: 18
                cardRadius: Style.settings.labelRadius
                text: {
                    const option = searchPage.sourceOptions()[choice]
                    return option ? option.displayName : ""
                }
                model: searchPage.sourceOptions().map(option => option.displayName)
                enabled: musicAdapter && searchPage.sourceOptions().length > 0
                onTransformed: (choice) => {
                    searchPage.selectSource(choice)
                    window.exitIndex = 1
                }
            }
        }

        QBlurTapBar {
            objectName: "searchTabs"
            x: 0
            y: 12
            z: 5
            model: ["歌曲", "歌单", "专辑", "歌词"]
            tabWidth: 80
            width: 324
            rectXy: Qt.rect(0, 12, width, 40)
            blurSource: searchChildPage.pageList[searchChildPage.lastIndex]
            onTabChange: (index) => {
                searchChildPage.stack(index)
                searchPage.searchTab = index
                if (musicAdapter)
                    musicAdapter.search(mainSearchInput.text, index)
            }
        }

        QListView {
            id: searchSong
            objectName: "searchSongsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: searchPage.modelFor("searchSongs")
            clip: true
            topMargin: 72
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText0ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchSong, index)).canEnqueue ? "\uf095" : ""
            }
            toolText1ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchSong, index)).canFavorite ? "\uf0c8" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 3
            onClicked: (index) => {
                const row = searchPage.rowFor(searchSong, index)
                if (searchPage.capabilitiesFor(row).canPlay)
                    musicAdapter.play(row)
            }
            onToolClicked: (index, tool) => {
                const row = searchPage.rowFor(searchSong, index)
                const caps = searchPage.capabilitiesFor(row)
                if (tool === 0 && caps.canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && caps.canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
        }

        QListView {
            id: searchLists
            objectName: "searchListsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: searchPage.modelFor("searchLists")
            clip: true
            visible: false
            topMargin: 72
            bottomMargin: 24
            isList: true
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText1ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchLists, index)).canFavorite ? "\uf0c8" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 3
            onClicked: (index) => searchPage.browseResult(searchPage.rowFor(searchLists, index))
            onToolClicked: (index, tool) => {
                const row = searchPage.rowFor(searchLists, index)
                if (tool === 1 && searchPage.capabilitiesFor(row).canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
        }

        QListView {
            id: searchAlbum
            objectName: "searchAlbumsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: searchPage.modelFor("searchAlbums")
            clip: true
            visible: false
            topMargin: 72
            bottomMargin: 24
            isList: true
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText0ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchAlbum, index)).canEnqueue ? "\uf095" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 3
            onClicked: (index) => searchPage.browseResult(searchPage.rowFor(searchAlbum, index))
            onToolClicked: (index, tool) => {
                const row = searchPage.rowFor(searchAlbum, index)
                if (tool === 0 && searchPage.capabilitiesFor(row).canEnqueue)
                    musicAdapter.enqueue(row)
            }
        }

        QListView {
            id: searchLyrics
            objectName: "searchLyricsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: searchPage.modelFor("searchLyrics")
            clip: true
            visible: false
            topMargin: 72
            bottomMargin: 24
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText0ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchLyrics, index)).canEnqueue ? "\uf095" : ""
            }
            toolText1ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchLyrics, index)).canFavorite ? "\uf0c8" : ""
            }
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 3
            onClicked: (index) => {
                const row = searchPage.rowFor(searchLyrics, index)
                if (searchPage.capabilitiesFor(row).canPlay)
                    musicAdapter.play(row)
            }
            onToolClicked: (index, tool) => {
                const row = searchPage.rowFor(searchLyrics, index)
                const caps = searchPage.capabilitiesFor(row)
                if (tool === 0 && caps.canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && caps.canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
        }
    }

    Item {
        id: searchAdapterDetailWindow
        objectName: "searchAdapterDetailWindow"
        z: 20
        anchors.fill: parent
        visible: false
        function opened(info) { visible = true }
        Connections {
            target: window
            enabled: searchAdapterDetailWindow.visible
            function onExit() {
                if (window.exitIndex <= 1)
                    searchAdapterDetailWindow.visible = false
            }
        }
        Rectangle {
            anchors.fill: parent
            color: Style.themes.primaryColor
        }
        QListView {
            objectName: "searchAdapterDetailList"
            id: searchDetailList
            x: 24
            y: 184
            width: searchAdapterDetailWindow.width - 32
            height: searchAdapterDetailWindow.height - 184
            model: searchPage.modelFor("categoryItems")
            clip: true
            topMargin: 8
            bottomMargin: 24
            menuModel: []
            toolText0: ""
            toolText1: ""
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 1
            toolText0ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchDetailList, index)).canEnqueue ? "\uf095" : ""
            }
            toolText1ForRow: function(index) {
                return searchPage.capabilitiesFor(searchPage.rowFor(searchDetailList, index)).canFavorite ? "\uf0c8" : ""
            }
            onClicked: (index) => {
                const row = searchPage.rowFor(searchDetailList, index)
                if (searchPage.capabilitiesFor(row).canPlay)
                    musicAdapter.play(row)
            }
            onToolClicked: (index, tool) => {
                const row = searchPage.rowFor(searchDetailList, index)
                const caps = searchPage.capabilitiesFor(row)
                if (tool === 0 && caps.canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && caps.canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
        }
    }
}
