// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import 'qrc:/QueMusic/components'

Item {
    id: favouritePage
    property var musicAdapter: null
    property var playbackAdapter: null
    property int setMode: 0
    property list<int> chooseIndex: []

    function capabilitiesFor(row) {
        return musicAdapter && row ? musicAdapter.capabilities(row) : ({})
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
        return musicAdapter ? musicAdapter.capabilities(selectedRows(model)) : ({})
    }

    function modelCapabilities(model) {
        if (!musicAdapter || !model) return ({})
        var rows = []
        for (var i = 0; i < model.count; ++i)
            rows.push(model.get(i))
        return musicAdapter.capabilities(rows)
    }

    function sectionFor(model) {
        if (!model) return ""
        if (model.count > 0)
            return model.get(model.count - 1).sectionId || ""
        return model.sectionId || ""
    }

    function retryCurrentSection() {
        if (!musicAdapter) return
        var model = favouriteChildPage.lastIndex === 1
                ? musicAdapter.favoriteLists : musicAdapter.favoriteSongs
        musicAdapter.retry(2, sectionFor(model))
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
            width: favouriteChildPage.width + 16
            height: favouriteChildPage.height
            model: musicAdapter ? musicAdapter.favoriteSongs : favoritesSong
            clip: true
            topMargin: 72
            selectedIndices: favouritePage.chooseIndex
            menuModel: musicAdapter ? [] : ["下载到本地","分享","歌曲信息"]
            toolText0: musicAdapter ? "" : "\uf095"
            toolText1: musicAdapter ? "" : "\uf0c8"
            toolText0ForRow: musicAdapter ? function(index) {
                return favouritePage.capabilitiesFor(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            toolText1ForRow: musicAdapter ? function(index) {
                return favouritePage.capabilitiesFor(model.get(index)).canUnfavorite ? "\uf0c8" : ""
            } : null
            sectionId: musicAdapter ? favouritePage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(2, sectionId) } : null

            onEnded: {
                if (musicAdapter && sectionId)
                    if (hasMore && !loadingMore)
                        musicAdapter.loadMore(2, sectionId)
            }

            onClicked: (index) => {
                if (favouritePage.setMode === 1) {
                    var idx = favouritePage.chooseIndex.indexOf(index);
                    if (idx === -1) {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.concat([index]);
                    } else {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.filter(v => v !== index);
                    }
                } else {
                    var row = model.get(index)
                    if (musicAdapter) {
                        if (capabilitiesFor(row).canPlay)
                            musicAdapter.play(row)
                    } else {
                        MusicApi.getMusicInfo(row.id, 0, row.source);
                    }
                }
            }
            onToolClicked: (index,tool) => {
                switch(tool) {
                case 0:
                    var row = model.get(index);
                    if (musicAdapter) {
                        if (capabilitiesFor(row).canEnqueue)
                            musicAdapter.enqueue(row);
                        break;
                    }
                    var listIndex = -1;
                    var indexHash = model.get(index).id;
                    for(var i = 0;i < playListModel.count;i++) {
                        var forUrl = playListModel.get(i).path;
                        if(forUrl === indexHash) {
                            listIndex = i;
                        }
                    }
                    if (listIndex == -1) {
                        playListModel.append({ name: model.get(index).title, path: model.get(index).id, songer: model.get(index).artist, source: model.get(index).source });
                        mainWarn.tiped("成功加入播放列表",1);
                    }
                    break;
                case 1:
                    var favoriteRow = model.get(index);
                    if (musicAdapter) {
                        if (capabilitiesFor(favoriteRow).canUnfavorite)
                            musicAdapter.setFavorite(favoriteRow, false);
                    } else {
                        favoritesSong.removeFavorite(favoriteRow.id, "song");
                        mainWarn.tiped("取消收藏",0);
                    }
                }
            }
            onMenuClicked: (index,choice) => {
                switch(choice) {
                case 0:
                    if (!musicAdapter)
                        MusicApi.getMusicInfo(model.get(index).id,1,model.get(index).source);
                    break;
                }
            }
            Text {
                anchors.centerIn: parent
                visible: (musicAdapter ? musicAdapter.favoriteSongs : favoritesSong).count === 0
                text: "没有收藏的内容？快去收藏一些歌曲吧"
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }
        QListView {
            id: lists
            width: favouriteChildPage.width + 16
            height: favouriteChildPage.height
            model: musicAdapter ? musicAdapter.favoriteLists : favoritesList
            clip: true
            isList: true
            topMargin: 72
            visible: false
            selectedIndices: favouritePage.chooseIndex
            menuModel: musicAdapter ? [] : ["下载到本地","分享","歌曲信息"]
            toolText0: ""
            toolText1: musicAdapter ? "" : "\uf0c8"
            toolText1ForRow: musicAdapter ? function(index) {
                return favouritePage.capabilitiesFor(model.get(index)).canUnfavorite ? "\uf0c8" : ""
            } : null
            sectionId: musicAdapter ? favouritePage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(2, sectionId) } : null

            onEnded: {
                if (musicAdapter && sectionId)
                    if (hasMore && !loadingMore)
                        musicAdapter.loadMore(2, sectionId)
            }

            onClicked: (index) => {
                if (favouritePage.setMode === 1) {
                    var idx = favouritePage.chooseIndex.indexOf(index);
                    if (idx === -1) {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.concat([index]);
                    } else {
                        favouritePage.chooseIndex = favouritePage.chooseIndex.filter(v => v !== index);
                    }
                } else {
                    var row = model.get(index)
                    if (musicAdapter) {
                        if (capabilitiesFor(row).canBrowse && musicAdapter.browse(row)) {
                            favoriteAdapterDetailWindow.opened(row)
                            mainContent.contentIndexed(1)
                            window.exitIndex = 1;
                        }
                    } else {
                        MusicApi.playlistSong.clear();
                        MusicApi.globalid = row.id;
                        MusicApi.getPlaylistSongs(row.id,1,20,row.source);
                        playListSongsWindow.songSource = row.source;
                        playListSongsWindow.opened(row);
                        window.exitIndex = 1;
                    }
                }
            }
            onToolClicked: (index,tool) => {
                switch(tool) {
                case 1:
                    var listRow = model.get(index);
                    if (musicAdapter) {
                        if (capabilitiesFor(listRow).canUnfavorite)
                            musicAdapter.setFavorite(listRow, false);
                    } else {
                        favoritesList.removeFavorite(listRow.id, "playlist");
                        mainWarn.tiped("取消收藏",0);
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: (musicAdapter ? musicAdapter.favoriteLists : favoritesList).count === 0
                text: "没有收藏的内容？快去收藏一些歌单吧"
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }
        Item {
            id: singer
            visible: false
            width: favouriteChildPage.width
            height: favouriteChildPage.height
            Text {
                anchors.centerIn: parent
                text: "喜欢的歌手"
                color: Style.themes.textColor
                font.pixelSize: 14
            }
        }
        Item {
            id: history
            visible: false
            width: favouriteChildPage.width
            height: favouriteChildPage.height
            Text {
                anchors.centerIn: parent
                text: "历史记录"
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
            visible: favouritePage.setMode !== 0
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
                        if (musicAdapter) {
                            var adapterModel = favouriteChildPage.lastIndex === 1
                                ? musicAdapter.favoriteLists : musicAdapter.favoriteSongs
                            if (selectedCapabilities(adapterModel).canUnfavorite) {
                                var rows = selectedRows(adapterModel)
                                for (var selected = 0; selected < rows.length; ++selected)
                                    musicAdapter.setFavorite(rows[selected], false)
                                favouritePage.chooseIndex = []
                            }
                            return
                        }
                        switch(favouritePage.setMode) {
                        case 1:
                            globalDialog.openSimpleDialog("取消收藏", "这将取消收藏这些歌曲",
                                function() {
                                    for(var i=0;i<favouritePage.chooseIndex.length;i++) {
                                        favoritesSong.removeFavorite(favoritesSong.get(favouritePage.chooseIndex[i]).id, "song");
                                    }
                                    favouritePage.chooseIndex = [];
                                    Style.warned("成功取消" + favouritePage.chooseIndex.length + "个收藏歌曲",1);
                                }
                            );
                            break;
                        case 2:
                            globalDialog.openSimpleDialog("取消收藏", "这将取消收藏这些歌单",
                                function() {
                                    for(var i=0;i<favouritePage.chooseIndex.length;i++) {
                                        favoritesList.removeFavorite(favoritesList.get(favouritePage.chooseIndex[i]).id, "playlist");
                                    }
                                    favouritePage.chooseIndex = [];
                                    Style.warned("成功取消" + favouritePage.chooseIndex.length + "个收藏歌单",1);
                                }
                            );
                            break;
                        default:
                            break;
                        }
                    }
                }
                QButton {
                    shadowEnabled: false
                    height: 36
                    radius: 20
                    text: "加入播放列表"
                    onClicked: {
                        if (musicAdapter) {
                            if (favouriteChildPage.lastIndex === 1)
                                return
                            var adapterSongs = musicAdapter.favoriteSongs
                            if (selectedCapabilities(adapterSongs).canEnqueue) {
                                var rows = selectedRows(adapterSongs)
                                for (var selected = 0; selected < rows.length; ++selected)
                                    musicAdapter.enqueue(rows[selected])
                            }
                            return
                        }
                        switch(favouritePage.setMode) {
                        case 1:
                            var playlist = [];
                            for(var i = 0;i < playListModel.count;i++) {
                                playlist.push(playListModel.get(i).path);
                            }
                            for(var a = 0;a < favouritePage.chooseIndex.length;a++) {
                                var listIndex = -1;
                                for(var b = 0;b < playlist.length;b++) {
                                    if(favoritesSong.get(favouritePage.chooseIndex[a]).id == playlist[b]) {
                                        listIndex = b;
                                        break;
                                    }
                                }
                                if (listIndex == -1) {
                                    playListModel.append({ name: favoritesSong.get(favouritePage.chooseIndex[a]).title, path: favoritesSong.get(favouritePage.chooseIndex[a]).id, songer: favoritesSong.get(favouritePage.chooseIndex[a]).artist, source: playListSongsWindow.songSource });
                                    mainWarn.tiped("成功加入播放列表",1);
                                }
                            }
                            break;
                        default:
                            break;
                        }
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

    PlayListWindow {
        id: playListSongsWindow
        mainTarget: favouriteChildPage
        winIndex: 1
        content: Item {

            QListView {
                id: playListsView
                x: 24
                y: 184
                width: playListSongsWindow.width - 32
                height: playListSongsWindow.height - 184
                model: MusicApi.playlistSong
                clip: true
                //reuseItems: true
                topMargin: 8
                bottomMargin: 24

                onClicked: (index) => {
                    if(Options.settings.soundQuality === 0) {
                        MusicApi.getMusicInfo(model.get(index).hash,0,playListSongsWindow.songSource);
                    } else if(Options.settings.soundQuality === 1) {
                        MusicApi.getMusicInfo(model.get(index).hashhq,0,playListSongsWindow.songSource);
                    } else {
                        MusicApi.getMusicInfo(model.get(index).hashsq,0,playListSongsWindow.songSource);
                    }
                }
                onToolClicked: (index,tool) => {
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
                            playListModel.append({ name: model.get(index).title, path: model.get(index).hash, songer: model.get(index).artist, source: playListSongsWindow.songSource });
                            mainWarn.tiped("成功加入播放列表",1);
                        }
                        break;
                    case 1:
                        if (favoritesSong.isFavorite(model.get(index).hash, "song")) {
                            favoritesSong.removeFavorite(model.get(index).hash, "song");
                            mainWarn.tiped("取消收藏",0);
                        } else {
                            favoritesSong.addFavorite(model.get(index).hash, model.get(index).title, model.get(index).artist, model.get(index).cover, playListSongsWindow.songSource, model.get(index).duration, "song");
                            mainWarn.tiped("成功收藏",1);
                        }
                        break;
                    }
                }

                onEnded: {
                    if(MusicApi.playlistSong.count % 20 === 0 && MusicApi.playlistSong.count !== 0) {
                        var tagid = playListSongsWindow.id;
                        MusicApi.getPlaylistSongs(tagid,MusicApi.playlistSong.count / 20 + 1,20,playListSongsWindow.songSource);
                        isEnd = false;
                    } else {
                        if(MusicApi.playlistSong.count !== 0) {
                            isEnd = true;
                        }
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
        QListView {
            objectName: "favoriteAdapterDetailList"
            x: 24
            y: 184
            width: favoriteAdapterDetailWindow.width - 32
            height: favoriteAdapterDetailWindow.height - 184
            model: musicAdapter ? musicAdapter.categoryItems : null
            clip: true
            topMargin: 8
            bottomMargin: 24
            menuModel: []
            toolText0: ""
            toolText1: ""
            sectionId: musicAdapter ? favouritePage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(1, sectionId) } : null
            toolText0ForRow: musicAdapter ? function(index) {
                return favouritePage.capabilitiesFor(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            toolText1ForRow: musicAdapter ? function(index) {
                return favouritePage.capabilitiesFor(model.get(index)).canFavorite ? "\uf0c8" : ""
            } : null
            onClicked: (index) => {
                if (!musicAdapter) return
                var row = model.get(index)
                if (favouritePage.capabilitiesFor(row).canPlay)
                    musicAdapter.play(row)
            }
            onToolClicked: (index, tool) => {
                if (!musicAdapter) return
                var row = model.get(index)
                if (tool === 0 && favouritePage.capabilitiesFor(row).canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && favouritePage.capabilitiesFor(row).canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
            onEnded: {
                if (musicAdapter && sectionId && hasMore && !loadingMore)
                    musicAdapter.loadMore(1, sectionId)
            }
        }
    }

}
