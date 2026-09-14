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
        if (musicAdapter && musicAdapter.sourceOptions)
            return musicAdapter.sourceOptions
        return [
            { sourceInstanceId: "", displayName: "酷狗音乐", available: true },
            { sourceInstanceId: "", displayName: "网易云音乐", available: true },
            { sourceInstanceId: "", displayName: "QQ音乐(x)", available: true },
            { sourceInstanceId: "", displayName: "自定义源(x)", available: true }
        ]
    }

    function sourceChoice() {
        if (!musicAdapter)
            return MusicApi.songSource
        var options = sourceOptions()
        for (var i = 0; i < options.length; ++i) {
            if (options[i].sourceInstanceId === musicAdapter.selectedSourceInstanceId)
                return i
        }
        return 0
    }

    function selectSource(choice) {
        if (musicAdapter) {
            var option = sourceOptions()[choice]
            if (option && option.available)
                musicAdapter.selectedSourceInstanceId = option.sourceInstanceId
            return
        }
        MusicApi.songSource = choice
        MusicApi.searchSongsResults.clear()
        MusicApi.searchSongs(mainSearchInput.text, MusicApi.nowIndex, 1, 20)
    }

    function capabilitiesFor(row) {
        return musicAdapter && row ? musicAdapter.capabilities(row) : ({})
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

    function currentModel() {
        if (!musicAdapter) return null
        switch (searchTab) {
        case 1: return musicAdapter.searchLists
        case 2: return musicAdapter.searchAlbums
        case 3: return musicAdapter.searchLyrics
        default: return musicAdapter.searchSongs
        }
    }

    function retryCurrentSection() {
        if (musicAdapter)
            musicAdapter.retry(3, sectionFor(currentModel()))
    }

    Component.onCompleted: {
        if (musicAdapter) {
            musicAdapter.activatePage(3)
            musicAdapter.search(mainSearchInput.text, searchTab)
        }
    }


    QPages {
        x: 24
        y: 68
        width: parent.width - 48
        height: parent.height - 68
        id: searchChildPage
        pageList: [searchSong,searchLists,searchAlbum,searchLyrics]

        // 顶部常显示栏
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
                x: parent.width - 96
                y: 0
                height: 36; width: 120
                //radius: 18
                anchors.right: parent.right
                choice: searchPage.sourceChoice()
                textColor: searchPage.sourceChoice() == 0 ? "#0F3975" : searchPage.sourceChoice() == 1 ? "#750F0F" : searchPage.sourceChoice() == 2 ? "#16750F" : "#756F0F"
                color: searchPage.sourceChoice() == 0 ? "#CDE8FF" : searchPage.sourceChoice() == 1 ? "#FFCDCD" : searchPage.sourceChoice() == 2 ? "#CDFFCD" : "#FFFFCD"
                border.color: searchPage.sourceChoice() == 0 ? "#4384F5" : searchPage.sourceChoice() == 1 ? "#F54343" : searchPage.sourceChoice() == 2 ? "#4DF543" : "#F5F543"
                radius: 18
                cardRadius: Style.settings.labelRadius
                text: {
                    var option = searchPage.sourceOptions()[choice]
                    return option ? option.displayName : ""
                }
                model: searchPage.sourceOptions().map(function(option) { return option.displayName })
                visible: true
                onTransformed: (choiced) => {
                    searchPage.selectSource(choiced)
                    window.exitIndex = 1;
                }
            }
        }

        QBlurTapBar {
            objectName: "searchTabs"
            x: 0
            y: 12
            z: 5
            model: ["歌曲","歌单","专辑","歌词"]
            tabWidth: 80
            width: 324
            rectXy: Qt.rect(0, 12, width, 40)
            blurSource: searchChildPage.pageList[searchChildPage.lastIndex]
            onTabChange: (index) => {
                if (musicAdapter) {
                    searchChildPage.stack(index)
                    searchPage.searchTab = index
                    MusicApi.nowIndex = index
                    musicAdapter.search(mainSearchInput.text, index)
                    return
                }
                MusicApi.searchSongsResults.clear()
                searchChildPage.stack(index)
                MusicApi.nowIndex = index
                MusicApi.searchSongs(mainSearchInput.text,index,1,20)
            }
        }

        QListView {
            id: searchSong
            objectName: "searchSongsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: musicAdapter ? musicAdapter.searchSongs : MusicApi.searchSongsResults
            clip: true
            topMargin: 72
            menuModel: musicAdapter ? [] : ["下载到本地","分享","歌曲信息"]
            toolText0: musicAdapter ? "" : "\uf095"
            toolText1: musicAdapter ? "" : "\uf0c8"
            toolText0ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            toolText1ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canFavorite ? "\uf0c8" : ""
            } : null
            sectionId: musicAdapter ? searchPage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(3, sectionId) } : null

            onEnded: {
                if (musicAdapter) {
                    if (sectionId)
                        if (hasMore && !loadingMore)
                            musicAdapter.loadMore(3, sectionId)
                    return
                }
                if(MusicApi.searchSongsResults.count % 20 === 0 && MusicApi.searchSongsResults.count !== 0) {
                    MusicApi.searchSongs(mainSearchInput.text,0,MusicApi.searchSongsResults.count / 20 + 1,20);
                    isEnd = false;
                } else {
                    if(MusicApi.searchSongsResults.count !== 0) {
                        isEnd = true;
                    }
                }
            }

            onClicked: (index) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (capabilitiesFor(adapterRow).canPlay)
                        musicAdapter.play(adapterRow)
                    return
                }
                if(Options.settings.soundQuality === 0) {
                    MusicApi.getMusicInfo(model.get(index).hash);
                } else if(Options.settings.soundQuality === 1) {
                    MusicApi.getMusicInfo(model.get(index).hashhq);
                } else {
                    MusicApi.getMusicInfo(model.get(index).hashsq);
                }
            }
            onToolClicked: (index,tool) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (tool === 0 && capabilitiesFor(adapterRow).canEnqueue)
                        musicAdapter.enqueue(adapterRow)
                    else if (tool === 1 && capabilitiesFor(adapterRow).canFavorite)
                        musicAdapter.setFavorite(adapterRow, true)
                    return
                }
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
                case 1:
                    if (favoritesSong.isFavorite(model.get(index).hash, "song")) {
                        favoritesSong.removeFavorite(model.get(index).hash, "song");
                        mainWarn.tiped("取消收藏",0);
                    } else {
                        favoritesSong.addFavorite(model.get(index).hash, model.get(index).title, model.get(index).artist, model.get(index).cover, MusicApi.songSource, model.get(index).duration, "song");
                        mainWarn.tiped("成功收藏",1);
                    }
                    break;
                }
            }
            onMenuClicked: (index,choice) => {
                if (musicAdapter)
                    return
                switch(choice) {
                case 0:
                    if(Options.settings.soundQuality === 0) {
                        MusicApi.getMusicInfo(model.get(index).hash,1);
                    } else if(Options.settings.soundQuality === 1) {
                        MusicApi.getMusicInfo(model.get(index).hashhq,1);
                    } else {
                        MusicApi.getMusicInfo(model.get(index).hashsq,1);
                    }
                    break;
                }
            }
        }
        QListView {
            id: searchLists
            objectName: "searchListsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: musicAdapter ? musicAdapter.searchLists : MusicApi.searchSongsResults
            clip: true
            visible: false
            topMargin: 72
            bottomMargin: 24
            isList: true
            menuModel: musicAdapter ? [] : ["下载到本地","分享","歌曲信息"]
            toolText0: ""
            toolText1: musicAdapter ? "" : "\uf0c8"
            toolText1ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canFavorite ? "\uf0c8" : ""
            } : null
            sectionId: musicAdapter ? searchPage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(3, sectionId) } : null

            onEnded: {
                if (musicAdapter) {
                    if (sectionId)
                        if (hasMore && !loadingMore)
                            musicAdapter.loadMore(3, sectionId)
                    return
                }
                if(MusicApi.searchSongsResults.count % 20 === 0 && MusicApi.searchSongsResults.count !== 0) {
                    MusicApi.searchSongs(mainSearchInput.text,1,MusicApi.searchSongsResults.count / 20 + 1,20);
                    isEnd = false;
                } else {
                    if(MusicApi.searchSongsResults.count !== 0) {
                        isEnd = true;
                    }
                }
            }

            onClicked: (index) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (capabilitiesFor(adapterRow).canBrowse && musicAdapter.browse(adapterRow)) {
                        searchAdapterDetailWindow.opened(adapterRow)
                        mainContent.contentIndexed(1)
                        window.exitIndex = 1
                    }
                    return
                }
                MusicApi.playlistSong.clear();
                MusicApi.globalid = model.get(index).hash;
                MusicApi.getPlaylistSongs(model.get(index).hash,1,20);
                //var image = model.get(index).cover.replace("{size}", "256") || "qrc:/QueMusic/resources/app/musicpic.png";
                //var title = model.get(index).title;
                playListSongsWindow.opened(model.get(index));
                window.exitIndex = 2;
            }
            onToolClicked: (index,tool) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (tool === 1 && capabilitiesFor(adapterRow).canFavorite)
                        musicAdapter.setFavorite(adapterRow, true)
                    return
                }
                switch(tool) {
                case 1:
                    if (favoritesList.isFavorite(model.get(index).hash, "playlist")) {
                        favoritesList.removeFavorite(model.get(index).hash, "playlist");
                        mainWarn.tiped("取消收藏",0);
                    } else {
                        favoritesList.addFavorite(model.get(index).hash, model.get(index).title, model.get(index).artist, model.get(index).cover, MusicApi.songSource, model.get(index).duration, "playlist");
                        mainWarn.tiped("成功收藏",1);
                    }
                    break;
                }
            }
        }
        QListView {
            id: searchAlbum
            objectName: "searchAlbumsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: musicAdapter ? musicAdapter.searchAlbums : MusicApi.searchSongsResults
            clip: true
            visible: false
            topMargin: 72
            bottomMargin: 24
            isList: true
            menuModel: musicAdapter ? [] : ["下载到本地","分享","歌曲信息"]
            toolText0: musicAdapter ? "" : "\uf095"
            toolText0ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            sectionId: musicAdapter ? searchPage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(3, sectionId) } : null
            toolText1: ""

            onEnded: {
                if (musicAdapter) {
                    if (sectionId)
                        if (hasMore && !loadingMore)
                            musicAdapter.loadMore(3, sectionId)
                    return
                }
                if(MusicApi.searchSongsResults.count % 20 === 0 && MusicApi.searchSongsResults.count !== 0) {
                    MusicApi.searchSongs(mainSearchInput.text,2,MusicApi.searchSongsResults.count / 20 + 1,20);
                    isEnd = false;
                } else {
                    if(MusicApi.searchSongsResults.count !== 0) {
                        isEnd = true;
                    }
                }
            }
            onClicked: (index) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (capabilitiesFor(adapterRow).canBrowse && musicAdapter.browse(adapterRow)) {
                        searchAdapterDetailWindow.opened(adapterRow)
                        mainContent.contentIndexed(1)
                        window.exitIndex = 1
                    }
                    return
                }
                MusicApi.getMusicInfo(model.get(index).hash);
            }
            onToolClicked: (index,tool) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (tool === 0 && capabilitiesFor(adapterRow).canEnqueue)
                        musicAdapter.enqueue(adapterRow)
                    return
                }
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
        }
        QListView {
            id: searchLyrics
            objectName: "searchLyricsList"
            width: searchChildPage.width + 16
            height: searchChildPage.height
            model: musicAdapter ? musicAdapter.searchLyrics : MusicApi.searchSongsResults
            clip: true
            visible: false
            topMargin: 72
            bottomMargin: 24
            menuModel: musicAdapter ? [] : ["下载到本地","分享","歌曲信息"]
            toolText0: musicAdapter ? "" : "\uf095"
            toolText1: musicAdapter ? "" : "\uf0c8"
            toolText0ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            toolText1ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canFavorite ? "\uf0c8" : ""
            } : null
            sectionId: musicAdapter ? searchPage.sectionFor(model) : ""
            hasMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).hasMore : model.hasMore) : true
            loadingMore: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).loadingMore : model.loadingMore) : false
            sectionError: musicAdapter ? (model.count > 0 ? model.get(model.count - 1).error : model.error) : ({})
            retryAction: musicAdapter ? function(sectionId) { musicAdapter.retry(3, sectionId) } : null

            onEnded: {
                if (musicAdapter) {
                    if (sectionId)
                        if (hasMore && !loadingMore)
                            musicAdapter.loadMore(3, sectionId)
                    return
                }
                if(MusicApi.searchSongsResults.count % 20 === 0 && MusicApi.searchSongsResults.count !== 0) {
                    MusicApi.searchSongs(mainSearchInput.text,3,MusicApi.searchSongsResults.count / 20 + 1,20);
                    isEnd = false;
                } else {
                    if(MusicApi.searchSongsResults.count !== 0) {
                        isEnd = true;
                    }
                }
            }
            onClicked: (index) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (capabilitiesFor(adapterRow).canPlay)
                        musicAdapter.play(adapterRow)
                    return
                }
                if(Options.settings.soundQuality === 0) {
                    MusicApi.getMusicInfo(model.get(index).hash);
                } else if(Options.settings.soundQuality === 1) {
                    MusicApi.getMusicInfo(model.get(index).hashhq);
                } else {
                    MusicApi.getMusicInfo(model.get(index).hashsq);
                }
            }
            onToolClicked: (index,tool) => {
                if (musicAdapter) {
                    var adapterRow = model.get(index)
                    if (tool === 0 && capabilitiesFor(adapterRow).canEnqueue)
                        musicAdapter.enqueue(adapterRow)
                    else if (tool === 1 && capabilitiesFor(adapterRow).canFavorite)
                        musicAdapter.setFavorite(adapterRow, true)
                    return
                }
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
                case 1:
                    if (favoritesSong.isFavorite(model.get(index).hash, "song")) {
                        favoritesSong.removeFavorite(model.get(index).hash, "song");
                        mainWarn.tiped("取消收藏",0);
                    } else {
                        favoritesSong.addFavorite(model.get(index).hash, model.get(index).title, model.get(index).artist, model.get(index).cover, MusicApi.songSource, model.get(index).duration, "song");
                        mainWarn.tiped("成功收藏",1);
                    }
                    break;
                }
            }
            onMenuClicked: (index,choice) => {
                if (musicAdapter)
                    return
                switch(choice) {
                case 0:
                    if(Options.settings.soundQuality === 0) {
                        MusicApi.getMusicInfo(model.get(index).hash,1);
                    } else if(Options.settings.soundQuality === 1) {
                        MusicApi.getMusicInfo(model.get(index).hashhq,1);
                    } else {
                        MusicApi.getMusicInfo(model.get(index).hashsq,1);
                    }
                    break;
                }
            }
        }
    }

    PlayListWindow {
        id: playListSongsWindow
        mainTarget: searchChildPage
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
                //reuseItems: true
                topMargin: 8
                bottomMargin: 24

                onClicked: (index) => {
                    if(Options.settings.soundQuality === 0) {
                        MusicApi.getMusicInfo(model.get(index).hash);
                    } else if(Options.settings.soundQuality === 1) {
                        MusicApi.getMusicInfo(model.get(index).hashhq);
                    } else {
                        MusicApi.getMusicInfo(model.get(index).hashsq);
                    }
                }
                onToolClicked: (index,tool) => {
                    switch(tool) {
                    case 0:
                        playListModel.append({ name: model.get(index).title, path: model.get(index).hash, songer: model.get(index).artist, source: MusicApi.songSource });
                        mainWarn.tiped("成功加入播放列表",1);
                        break;
                    case 1:
                        if (favoritesSong.isFavorite(model.get(index).hash, "song")) {
                            favoritesSong.removeFavorite(model.get(index).hash, "song");
                            mainWarn.tiped("取消收藏",0);
                        } else {
                            favoritesSong.addFavorite(model.get(index).hash, model.get(index).title, model.get(index).artist, model.get(index).cover, MusicApi.songSource, model.get(index).duration, "song");
                            mainWarn.tiped("成功收藏",1);
                        }
                        break;
                    }
                }

                onEnded: {
                    if(MusicApi.playlistSong.count % 20 === 0 && MusicApi.playlistSong.count !== 0) {
                        var tagid = playListSongsWindow.id;
                        MusicApi.getPlaylistSongs(tagid,MusicApi.playlistSong.count / 20 + 1,20);
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
            x: 24
            y: 184
            width: searchAdapterDetailWindow.width - 32
            height: searchAdapterDetailWindow.height - 184
            model: musicAdapter ? musicAdapter.categoryItems : null
            clip: true
            topMargin: 8
            bottomMargin: 24
            toolText0ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canEnqueue ? "\uf095" : ""
            } : null
            toolText1ForRow: musicAdapter ? function(index) {
                return searchPage.capabilitiesFor(model.get(index)).canFavorite ? "\uf0c8" : ""
            } : null
            onClicked: (index) => {
                if (!musicAdapter) return
                var row = model.get(index)
                if (searchPage.capabilitiesFor(row).canPlay)
                    musicAdapter.play(row)
            }
            onToolClicked: (index, tool) => {
                if (!musicAdapter) return
                var row = model.get(index)
                if (tool === 0 && searchPage.capabilitiesFor(row).canEnqueue)
                    musicAdapter.enqueue(row)
                else if (tool === 1 && searchPage.capabilitiesFor(row).canFavorite)
                    musicAdapter.setFavorite(row, true)
            }
        }
    }
}
