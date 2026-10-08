// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic
import 'qrc:/QueMusic/components'

Item {
    id: playlistPage
    property var musicAdapter: null
    property var playbackAdapter: null
    //property alias animatedWindow: animationWrapper
    property real toolsWindow: 0
    //property bool displaytop: flickable.contentY > 60 ? true : false
    function sourceOptions() {
        return musicAdapter && musicAdapter.sourceOptions ? musicAdapter.sourceOptions : []
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
    function loadMoreCategory() {
        if (musicAdapter && typeof musicAdapter.loadMore === "function") {
            musicAdapter.loadMore(1, "")
            return true
        }
        return false
    }
    function browseCategory(index) {
        if (!musicAdapter || !musicAdapter.categoryItems) return false
        const row = musicAdapter.categoryItems.get(index)
        return browsePresentation(row)
    }
    function categoryGenres() {
        const rows = []
        if (!musicAdapter || !musicAdapter.categoryItems) return rows
        for (let i = 0; i < musicAdapter.categoryItems.count; ++i) {
            const row = musicAdapter.categoryItems.get(i)
            if (row.entityType === 4) rows.push(row)
        }
        return rows
    }
    function categoryPlaylists() {
        const rows = []
        if (!musicAdapter || !musicAdapter.categoryItems) return rows
        for (let i = 0; i < musicAdapter.categoryItems.count; ++i) {
            const row = musicAdapter.categoryItems.get(i)
            if (row.entityType === 3) rows.push(row)
        }
        return rows
    }
    function categoryArtists() {
        const rows = []
        if (!musicAdapter || !musicAdapter.categoryItems) return rows
        for (let i = 0; i < musicAdapter.categoryItems.count; ++i) {
            const row = musicAdapter.categoryItems.get(i)
            if (row.entityType === 2) rows.push(row)
        }
        return rows
    }
    function browseArtist(index) {
        return browsePresentation(categoryArtists()[index])
    }
    function browsePresentation(row) {
        if (!musicAdapter) return false
        if (!row || !musicAdapter.capabilities(row).canBrowse || !musicAdapter.browse(row))
            return false
        adapterDetailWindow.opened(row.title || "",
                                   row.cover || "qrc:/QueMusic/resources/app/musicpic.png")
        window.exitIndex = 2
        return true
    }
    Component.onCompleted: {
        if (musicAdapter)
            musicAdapter.activatePage(1)
    }

    QPages {
        id: playlistChildPage
        x: 24
        y: 68
        width: parent.width - 48
        height: parent.height - 68
        pageList: [musicsPage,musicMenuPage,cloud,album]

        // 顶部标题
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
                text: "分类"
                font.weight: Font.DemiBold
                font.pixelSize: Style.settings.pageTitle
                color: Style.themes.fontColor
            }
            QDrop {
                objectName: "categorySourceScope"
                x: parent.width - 120
                y: 0
                height: 36; width: 120
                //radius: 18
                anchors.right: parent.right
                choice: playlistPage.sourceChoice()
                textColor: Style.themes.textColor
                color: Style.themes.primaryColor
                border.color: Style.themes.borderColor
                radius: 18
                cardRadius: Style.settings.labelRadius
                text: {
                    const option = playlistPage.sourceOptions()[choice]
                    return option ? option.displayName : ""
                }
                model: playlistPage.sourceOptions().map((option) => option.displayName)
                enabled: musicAdapter && playlistPage.sourceOptions().length > 0
                onTransformed: (choiced) => {
                    playlistPage.selectSource(choiced)
                }
            }
        }

        QBlurTapBar {
            x: 0
            y: 12
            z: 12
            model: ["歌曲","歌单","排行榜","歌手"]
            tabWidth: 80
            width: 324
            rectXy: Qt.rect(0, 12, width, 40)
            blurSource: playlistChildPage.pageList[playlistChildPage.lastIndex]
            onTabChange: (index) => {
                playlistChildPage.stack(index)
                if ((index === 1 || index === 3) && musicAdapter)
                    musicAdapter.activatePage(1)
            }
        }

        QCard {
            x: parent.width - 148
            y: 12
            z: 11
            padding: 2
            width: 148
            height: 40
            cardColor: Style.themes.primaryBlurColor
            radius: 20

            Row {
                anchors.fill: parent
                //  刷新
                SButton {
                    objectName: "categoryRefreshButton"
                    width: 36
                    height: 36
                    radius: 18
                    iconCharacter: "\uf11b"
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    onClicked: {
                        if (musicAdapter && typeof musicAdapter.refreshPage === "function")
                            musicAdapter.refreshPage(1)
                    }
                }
                //  布局
                SButton {
                    width: 36
                    height: 36
                    radius: 18
                    iconCharacter: "\uf0d4"
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    onClicked: {

                    }
                }
                //  排序
                SButton {
                    width: 36
                    height: 36
                    radius: 18
                    iconCharacter: "\uf10b"
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    onClicked: {

                    }
                }
                // 筛选
                SButton {
                    width: 36
                    height: 36
                    radius: 18
                    iconCharacter: "\uf101"
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    onClicked: {

                    }
                }
            }
        }

        Item {
            id: musicsPage
            width: playlistChildPage.width
            height: playlistChildPage.height
            ListView {
                objectName: "categoryGenreFilters"
                width: parent.width
                height: 32
                y: 72
                spacing: 6
                orientation: ListView.Horizontal
                clip: true
                model: playlistPage.categoryGenres()
                delegate: Rectangle {
                    width: 96
                    height: 32
                    radius: 16
                    color: musicsMenuArea.containsMouse ? Style.themes.hoverColor : Style.themes.primaryColor
                    border.color: Style.themes.sideColor
                    border.width: 1
                    Text {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: modelData.title || ""
                        elide: Text.ElideRight
                        font.pixelSize: Style.settings.text
                        color: Style.themes.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: musicsMenuArea
                        hoverEnabled: true
                        anchors.fill: parent
                        onClicked: playlistPage.browsePresentation(modelData)
                    }
                }
            }
            QListView {
                id: searchSong
                objectName: "categoryList"
                width: parent.width + 16
                y: 104
                height: parent.height - 104
                model: musicAdapter ? musicAdapter.categoryItems : null
                clip: true
                menuModel: []
                toolText0: ""
                toolText1: ""
                toolText0ForRow: musicAdapter ? function(index) {
                    const row = searchSong.model && searchSong.model.get(index)
                    return row && musicAdapter.capabilities(row).canEnqueue ? "\uf095" : ""
                } : null
                onEnded: isEnd = !playlistPage.loadMoreCategory()
                onClicked: (index) => {
                    if (!musicAdapter || !searchSong.model) return
                    const row = searchSong.model.get(index)
                    if (musicAdapter.capabilities(row).canPlay)
                        musicAdapter.play(row)
                }
                onToolClicked: (index, tool) => {
                    if (!musicAdapter || !searchSong.model) return
                    const row = searchSong.model.get(index)
                    if (tool === 0 && musicAdapter.capabilities(row).canEnqueue)
                        musicAdapter.enqueue(row)
                }
            }
        }
        Item {
            id: musicMenuPage
            visible: false
            width: playlistChildPage.width
            height: playlistChildPage.height
            QListView {
                id: musicMenuList
                objectName: "categoryBrowseList"
                y: 72
                height: parent.height - y
                width: parent.width + 16
                clip: true
                isList: true
                showListCount: false
                model: playlistPage.categoryPlaylists()
                menuModel: []
                toolText0: ""
                toolText1: ""
                toolText1ForRow: function(index) {
                    const row = model[index]
                    return musicAdapter && row && musicAdapter.capabilities(row).canFavorite ? "\uf0c8" : ""
                }
                artistX: width / 2 - 50
                bottomMargin: 24
                onEnded: isEnd = !playlistPage.loadMoreCategory()
                onClicked: (index) => {
                    const row = model[index]
                    if (row) playlistPage.browsePresentation(row)
                }
                onToolClicked: (index, tool) => {
                    const row = model[index]
                    if (musicAdapter && row && tool === 1
                            && musicAdapter.capabilities(row).canFavorite)
                        musicAdapter.setFavorite(row, true)
                }
            }
        }
        Item {
            id: cloud
            visible: false
            width: playlistChildPage.width
            height: playlistChildPage.height
            Component.onCompleted: {
                if(MusicApi.toplistList.count === 0)
                    MusicApi.getAllToplist();
            }

            QScrollView {
                id: toplistFlick
                width: parent.width + 24
                height: parent.height
                contentChildren: Flow {
                    id: toplistFlow
                    width: parent.width
                    padding: 12
                    topPadding: 68
                    bottomPadding: 24
                    //height: implicitHeight + 640
                    spacing: 20
                    Repeater {
                        model: MusicApi.toplistList
                        delegate: Rectangle {
                            width: 156
                            height: 216
                            radius: Style.settings.labelRadius
                            color: Style.themes.primaryColor
                            scale: toplistCardArea.containsMouse ? 1.04 : 1.0
                            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutExpo } }
                            RectangularShadow {
                                anchors.fill: parent
                                z: -1
                                offset.x: 2
                                offset.y: 2
                                radius: Style.settings.labelRadius
                                blur: toplistCardArea.containsMouse ? 24 : 8
                                spread: 0
                                color: Style.themes.shadowColor
                                Behavior on blur { NumberAnimation { duration: 200 } }
                            }
                            QPicture {
                                width: 156
                                height: 156
                                source: model.cover.replace("{size}","128") || "qrc:/QueMusic/resources/app/musicpic.png"
                                radius: Style.settings.labelRadius
                                radius3: 0
                                radius4: 0
                                sourceSize: Qt.size(128,128)
                            }
                            // 平台徽标
                            Rectangle {
                                x: 8
                                y: 8
                                width: 50
                                height: 20
                                radius: 10
                                color: model.source === 0 ? "#CDE8FF" : "#FFCDCD"
                                Text {
                                    anchors.centerIn: parent
                                    text: model.source === 0 ? "酷狗" : "网易云"
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: model.source === 0 ? "#0F3975" : "#750F0F"
                                }
                            }
                            Text {
                                x: 12
                                y: 166
                                width: 132
                                text: model.title
                                font.bold: true
                                color: Style.themes.fontColor
                                font.pixelSize: Style.settings.textmain
                                elide: Text.ElideRight
                            }
                            Text {
                                x: 12
                                y: 190
                                width: 132
                                text: model.artist || ""
                                color: Style.themes.textColor
                                font.pixelSize: Style.settings.text
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: toplistCardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    MusicApi.playlistSong.clear();
                                    MusicApi.globalid = model.hash;
                                    MusicApi.getMusicToplist(1, 20, Number(model.hash), model.source);
                                    playListSongsWindow.opened(model);
                                    window.exitIndex = 2;
                                }
                            }
                        }
                    }
                }
            }
        }
        Item {
            id: album
            visible: false
            width: playlistChildPage.width
            height: playlistChildPage.height
            Flickable {
                id: singerFlick
                y: 72
                width: parent.width
                height: parent.height - y
                clip: true
                contentWidth: width
                contentHeight: singerColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Column {
                    id: singerColumn
                    width: parent.width
                    Flow {
                        width: parent.width
                        spacing: 20
                        Repeater {
                            objectName: "categoryArtistCards"
                            model: playlistPage.categoryArtists()
                            delegate: Item {
                                width: 96
                                height: 132
                                scale: singerArea.containsMouse ? 1.06 : 1.0
                                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutExpo } }
                                QPicture {
                                    width: 96
                                    height: 96
                                    radius: 48
                                    source: modelData.cover || "qrc:/QueMusic/resources/app/musicpic.png"
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 100
                                    width: parent.width
                                    text: modelData.title || ""
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: Style.settings.text
                                    color: Style.themes.textColor
                                }
                                MouseArea {
                                    id: singerArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: playlistPage.browseArtist(index)
                                }
                            }
                        }
                    }
                    Item {
                        width: parent.width
                        height: 60
                        QButton {
                            objectName: "categoryArtistsMoreButton"
                            anchors.centerIn: parent
                            height: 40
                            width: 120
                            radius: 20
                            iconCharacter: "\uf0f8"
                            text: "更多"
                            enabled: musicAdapter !== null
                            onClicked: playlistPage.loadMoreCategory()
                        }
                    }
                }
            }
        }
    }

    // 尚未迁移的旧榜单详情
    PlayListWindow {
        id: playListSongsWindow
        objectName: "playlistDetailWindow"
        mainTarget: playlistChildPage
        winIndex: 2
        content: Item {
            QListView {
                id: playListsView
                x: 24
                y: 184
                width: playListSongsWindow.width - 32
                height: playListSongsWindow.height - 184
                model: MusicApi.playlistSong
                clip: true
                topMargin: 8
                bottomMargin: 24
                onClicked: (index) => {
                    if (musicAdapter)
                        return
                    if(Options.settings.soundQuality === 0) {
                        MusicApi.getMusicInfo(model.get(index).hash);
                    } else if(Options.settings.soundQuality === 1) {
                        MusicApi.getMusicInfo(model.get(index).hashhq);
                    } else {
                        MusicApi.getMusicInfo(model.get(index).hashsq);
                    }
                }
                onToolClicked: (index,tool) => {
                    if (musicAdapter)
                        return
                    switch(tool) {
                    case 0:
                        var listIndex = -1;
                        var indexHash = model.get(index).hash;
                        for(var i = 0;i < playListModel.count;i++) {
                            var forUrl = playListModel.get(i).path;
                            if(forUrl === indexHash) {
                                listIndex = i;
                            }
                        }
                        if (listIndex == -1) {
                            playListModel.append({ name: model.get(index).title, path: model.get(index).hash, songer: model.get(index).artist, source: MusicApi.songSource });
                            mainWarn.tiped("成功加入播放列表",1);
                        }
                        break;
                    }
                }
                footer: Item {
                    height: 60
                    width: playListsView.width
                    QButton {
                        anchors.centerIn: parent
                        height: 40; width: 120
                        radius: 20
                        iconCharacter: "\uf0f8"
                        text: "更多"
                        onClicked: {
                            if (musicAdapter) return;
                            if(MusicApi.loadState) return;
                            var id = MusicApi.globalid;
                            var page = MusicApi.playlistSong.count / 20 + 1;
                            if(MusicApi.playlistSong.count % 20 === 0)
                                MusicApi.getMusicToplist(page, 20, id, MusicApi.songSource);
                            else
                                mainWarn.tiped("没有更多了",0);
                        }
                    }
                }
            }
        }
    }

    AnimatorWindow {
        id: adapterDetailWindow
        objectName: "adapterPlaylistDetailWindow"
        mainTarget: playlistChildPage
        winIndex: 2
        haveControl: false
        onVisibleChanged: {
            if (!visible && musicAdapter
                    && typeof musicAdapter.closeCategoryBrowse === "function")
                musicAdapter.closeCategoryBrowse()
        }
        content: QListView {
            objectName: "adapterPlaylistDetailList"
            x: 24
            y: 128
            width: adapterDetailWindow.width - 32
            height: adapterDetailWindow.height - 128
            model: musicAdapter ? musicAdapter.categoryItems : []
            menuModel: []
            toolText0: "\uf095"
            toolText1: ""
            toolText0ForRow: musicAdapter ? function(index) {
                return musicAdapter.capabilities(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            clip: true
            topMargin: 8
            bottomMargin: 24
            onClicked: (index) => {
                if (!musicAdapter) return
                const row = model.get(index)
                const caps = musicAdapter.capabilities(row)
                if (caps.canPlay)
                    musicAdapter.play(row)
                else if (caps.canBrowse)
                    playlistPage.browsePresentation(row)
            }
            onToolClicked: (index, tool) => {
                if (!musicAdapter || tool !== 0) return
                const row = model.get(index)
                if (musicAdapter.capabilities(row).canEnqueue)
                    musicAdapter.enqueue(row)
            }
            onMenuClicked: (index, choice) => {
                if (musicAdapter)
                    return
            }
            onEnded: {
                if (musicAdapter && typeof musicAdapter.loadMore === "function") {
                    musicAdapter.loadMore(1, "")
                    isEnd = false
                } else {
                    isEnd = true
                }
            }
        }
    }
}
