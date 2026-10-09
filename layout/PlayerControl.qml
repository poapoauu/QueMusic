// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Controls.Basic
import QueMusic 1.0
import 'qrc:/QueMusic/components'

//底部控制栏
Rectangle {
    id: musicControlMin
    y: parent.height - 78 + controlMaxLoader.hideHeight
    height: 78
    color: Style.themes.primaryBlurColor
    clip: false
    property int musicInfoX: 100
    property int cycleIndex: Options.playSettings.cycleIndex
    property int playerRateIndex: 2
    property bool legacyFavorite: false
    function syncLegacyFavorite() {
        if (window.sourceLyricsMode) return;
        const index = playListModel.playListIndex;
        if (index < 0 || index >= playListModel.count) { legacyFavorite = false; return; }
        const row = playListModel.get(index);
        legacyFavorite = !!(row && row.path && favoritesSong.isFavorite(row.path, "song"));
    }
    Component.onCompleted: syncLegacyFavorite()

    // 播放顺序持久化（跨平台：QSettings → 系统配置目录）
    onCycleIndexChanged: {
        if (Options.playSettings.cycleIndex !== cycleIndex)
            Options.playSettings.cycleIndex = cycleIndex
    }

    readonly property bool securePlaybackActive: window.securePlaybackCurrent
    readonly property real currentPosition: window.lyricsAdapter.position
    readonly property real currentDuration: window.lyricsAdapter.duration
    readonly property real currentPlaybackRate: window.lyricsAdapter.playbackRate
    readonly property string mediaTime: (Math.floor(currentPosition / 60000)) + ":" + (Math.floor(currentPosition / 1000) % 60)
    function formatTime(ms) {
        var seconds = Math.floor(ms / 1000);
        var minutes = Math.floor(ms / 60000);
        return minutes + ":" + (seconds % 60);
    }
    Connections {
        target: playListModel
        function onPlayListIndexChanged() {
            musicControlMin.syncLegacyFavorite();
        }
    }
    Connections {
        target: window
        function onSourceLyricsModeChanged() { sourceFavoriteMenu.close(); musicControlMin.syncLegacyFavorite(); }
    }

    Rectangle {
        width: musicControlMin.width
        height: 1
        color: Style.themes.sideColor
    }

    // 拆分多歌手，覆盖常见分隔符：/ 、 ， , & ; ；(不含空格，避免拆坏英文歌手名)
    function parseArtists(raw) {
        var parts = raw.split(/\s*[\/、,，&;&；]\s*/);
        var list = [];
        list.push("搜索")
        for(var i = 0; i < parts.length; i++) {
            var s = parts[i].trim();
            if(s && list.indexOf(s) === -1) {
                list.push(s);
            }
        }
        return list;
    }

    // 统一搜索入口
    function doSearchSongsMessage(name) {
        if (typeof name !== "string" || !name.trim() || !window.musicAdapter
                || typeof window.musicAdapter.search !== "function") return;
        mainSearchInput.text = name.trim();
        mainContent.contentIndexed(6);
        window.musicAdapter.search(mainSearchInput.text, 0);
        window.exitIndex = 1;
    }

    //控制条
    Item {
        id: sliderControl
        visible: window.lyricsAdapter.active
        x: 0
        y: -10
        z: 6
        width: musicControlMin.width
        height: 23
        clip: false
        Rectangle {
            z: 1
            x: 0
            y: -80
            height: 92
            width: musicControlMin.width
            opacity: progressSlider.hovered ? 0.2 : 0
            Behavior on opacity { NumberAnimation { duration: 100 } }

            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: Style.themes.textColor }
            }

        }

        Slider {
            z: 2
            id: progressSlider
            anchors.fill: parent
            width: musicControlMin.width
            from: 0
            to: musicControlMin.currentDuration > 0 ? musicControlMin.currentDuration : 1 // 避免除零错误
            enabled: window.lyricsAdapter.seekable
            Binding {
                target: progressSlider
                property: "value"
                value: musicControlMin.currentPosition
                when: !progressSlider.pressed
                restoreMode: Binding.RestoreNone
            }
            live: true
            padding: 0


            // 关键：用户拖动时，跳转播放位置
            onMoved: window.lyricsAdapter.seek(value)

            // 可选：在滑块手柄上显示预览时间
            ToolTip {
                parent: progressSlider.handle
                visible: progressSlider.pressed
                text: musicControlMin.mediaTime
                horizontalPadding: 8
                background: Rectangle {
                    anchors.fill: parent
                    color: Style.themes.primaryColor
                    border.width: 2
                    radius: height
                    border.color: Style.themes.secondaryColor
                }
            }
            // 背景轨道
            background: Rectangle {
                y: progressSlider.hovered ? 8 : 10
                x: 0
                width: musicControlMin.width
                height: progressSlider.hovered ? 6 : 2
                radius: 0
                color: Style.themes.sideColor

                // 已完成部分
                Rectangle {
                    width: progressSlider.visualPosition * sliderControl.width
                    height: parent.height
                    color: Style.themes.themeColor
                    radius: 0
                }
            }

            // 手柄
            handle: Rectangle {
                visible: progressSlider.hovered// ? 1 : 0
                x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth-width)
                y: 2
                implicitWidth: 18
                implicitHeight: 18
                radius: 18
                color: Style.themes.primaryColor
                border.color: Style.themes.themeColor
                border.width: 3
            }
        }
    }

    //音乐信息
    Item {
        id: musicinfo
        x: musicControlMin.musicInfoX
        y: 14
        z: 1
        clip: false
        width: 200
        height: 50

        Text {
            id: titleDisplay
            y: 0
            x: 0
            width: 128
            elide: Text.ElideRight
            height: 25
            text: window.musicTitle
            font.bold: true
            font.pixelSize: 15
            verticalAlignment: Text.AlignVCenter
            color: titleDisplayMouse.containsMouse ? Style.themes.themeColor : Style.themes.textColor

            MouseArea {
                id: titleDisplayMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if(!window.musicTitle)  return;
                    titleMenu.popup();
                }
            }
            // 为防止误触，使用点击弹出菜单再搜索
            QMenu {
                id: titleMenu
                model: ["搜索歌曲名"]
                masked: true
                blurSource: null // 位置特殊，关闭模糊效果
                onClicked: (index) => {
                    musicControlMin.doSearchSongsMessage(window.musicTitle);
                }
            }
        }
        Text {
            id: artistDisplay
            y: 25
            x: 0
            width: 128
            elide: Text.ElideRight
            height: 25
            text: window.musicArtist
            font.bold: false
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            color: artistDisplayMouse.containsMouse ? Style.themes.themeColor : Style.themes.textColor

            MouseArea {
                id: artistDisplayMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if(!window.musicArtist)  return; //本地音乐没有歌手信息时，忽略
                    var artists = musicControlMin.parseArtists(window.musicArtist);
                    artistMenu.model = artists;// 多歌手,弹菜单
                    artistMenu.popup();

                    //else {
                    //    doSearchSongsMessage(artists[0]);// 单歌手直接搜
                    //}

                }
            }
            //多位歌手时，显示菜单
            QMenu {
                id: artistMenu
                model: []
                masked: true
                blurSource: null // 位置特殊，关闭模糊效果
                onClicked: (index) => {
                    if(index === 0) {
                        musicControlMin.doSearchSongsMessage(window.musicArtist);
                    } else {
                        musicControlMin.doSearchSongsMessage(model[index]);
                    }
                }
            }
        }
        SButton {
            id: likeButton
            objectName: "currentPlaybackFavoriteButton"
            enabled: window.sourceLyricsMode ? window.lyricsAdapter.favoriteEnabled
                                            : (playListModel.playListIndex >= 0 && playListModel.playListIndex < playListModel.count)
            x: 132
            y: 5
            iconCharacter: "\uf0c8"
            width: 40
            height: 40
            radius: 40
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: (window.sourceLyricsMode ? window.lyricsAdapter.favorite.state === "favorite" : musicControlMin.legacyFavorite)
                       ? Style.themes.themeColor : Style.themes.textColor
            shadowEnabled: false
            onClicked: {
                if (window.sourceLyricsMode) {
                    if (window.lyricsAdapter.toggleFavorite() === "choose") {
                        const info = window.lyricsAdapter.favorite;
                        let labels = [], values = [];
                        if (info.canFavorite) { labels.push("收藏"); values.push(true); }
                        if (info.canUnfavorite) { labels.push("取消收藏"); values.push(false); }
                        sourceFavoriteMenu.playbackToken = info.token;
                        sourceFavoriteMenu.values = values;
                        sourceFavoriteMenu.model = labels;
                        sourceFavoriteMenu.popup();
                    }
                    return;
                }
                if (playListModel.playListIndex < 0 || playListModel.playListIndex >= playListModel.count) return;
                if(playListModel.get(playListModel.playListIndex).source !== -1) {
                    if (favoritesSong.isFavorite(playListModel.get(playListModel.playListIndex).path, "song")) {
                        favoritesSong.removeFavorite(playListModel.get(playListModel.playListIndex).path, "song");
                        mainWarn.tiped("取消收藏",0);
                        musicControlMin.legacyFavorite = false;
                    } else {
                        favoritesSong.addFavorite(playListModel.get(playListModel.playListIndex).path, window.musicTitle, window.musicArtist, mainMedia.urlStr, playListModel.get(playListModel.playListIndex).source, Math.floor(mainMedia.duration / 1000), "song");
                        mainWarn.tiped("成功收藏",1);
                        musicControlMin.legacyFavorite = true;
                    }
                }
            }
            QTip {
                visible: parent.hovered
                text: !window.sourceLyricsMode ? "收藏" : window.lyricsAdapter.favorite.pending ? "正在处理收藏"
                      : window.lyricsAdapter.favorite.failed ? "收藏操作失败，点击重试"
                      : window.lyricsAdapter.favorite.state === "favorite" ? "取消收藏"
                      : window.lyricsAdapter.favorite.state === "notFavorite" ? "收藏" : "收藏操作"
            }
            QMenu {
                id: sourceFavoriteMenu
                objectName: "currentPlaybackFavoriteMenu"
                property string playbackToken: ""
                property var values: []
                masked: true
                blurSource: null
                onClicked: (index) => {
                    if (index >= 0 && index < values.length)
                        window.lyricsAdapter.setFavorite(values[index], playbackToken);
                }
                Connections {
                    target: window.lyricsAdapter
                    function onFavoriteChanged() {
                        if (sourceFavoriteMenu.playbackToken && sourceFavoriteMenu.playbackToken !== window.lyricsAdapter.favorite.token)
                            sourceFavoriteMenu.close();
                    }
                }
            }
        }
        SButton {
            id: currentDownloadButton
            objectName: "currentPlaybackDownloadButton"
            // Current Source downloads need a capability-gated Host destination flow.
            // Until that flow is connected, never fall back to the legacy hash request.
            enabled: !window.sourceLyricsMode && playListModel.playListIndex >= 0
                     && playListModel.playListIndex < playListModel.count
                     && playListModel.get(playListModel.playListIndex).source !== -1
            x: 174
            y: 5
            //iconSize:
            iconCharacter: "\uf011"
            width: 40
            height: 40
            radius: 40
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            shadowEnabled: false
            onClicked: {
                if (window.sourceLyricsMode || !enabled) return;
                const row = playListModel.get(playListModel.playListIndex);
                if (row && typeof row.path === "string" && row.path)
                    MusicApi.getMusicInfo(row.path, 1);
            }
            QTip {
                visible: parent.hovered
                text: "下载"
            }
        }

    }

    //中间控制
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 16
        z: 3
        height: 46
        spacing: 4
        SButton {
            iconCharacter: ["\uf118","\uf115","\uf0e2","\uf03b"][musicControlMin.cycleIndex]
            width: 46
            height: 46
            radius: 46
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            shadowEnabled: false
            iconSize: Style.settings.texticon + 1
            onClicked: {
                if(musicControlMin.cycleIndex < 3) {
                    musicControlMin.cycleIndex += 1;
                } else {
                    musicControlMin.cycleIndex = 0;
                }
            }
            QTip {
                visible: parent.hovered
                text: "播放顺序"
            }
        }
        SButton {
            iconCharacter: "\uf0dc"
            width: 46
            height: 46
            radius: 46
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticonH
            shadowEnabled: false
            onClicked: musicControlMin.lastMedia()
            QTip {
                visible: parent.hovered
                text: "上一首"
            }
        }
        SButton {
            iconCharacter: window.lyricsAdapter.playing ? "\uf02f" : "\uf00e"
            width: 46
            height: 46
            radius: 46
            buttonColor: Style.themes.secondaryBlurColor
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticonH
            shadowEnabled: false
            onClicked: {
                window.togglePlayback();
            }
            QTip {
                visible: parent.hovered
                text: window.lyricsAdapter.playing ? "暂停" : "播放"
            }
        }
        SButton {
            iconCharacter: "\uf0d9"
            width: 46
            height: 46
            radius: 46
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticonH
            shadowEnabled: false
            onClicked: musicControlMin.enterMedia()
            QTip {
                visible: parent.hovered
                text: "下一首"
            }
        }
        SButton {
            iconCharacter: "\uf0d0"
            width: 46
            height: 46
            radius: 46
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticon + 1
            shadowEnabled: false
            onClicked: {
                if(playerOptionDialog.visible) {
                    playerOptionDialog.close();
                } else {
                    playerOptionDialog.open();
                }
            }
            QTip {
                visible: parent.hovered
                text: "播放器控制"
            }
        }

    }

    //右侧栏
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 24
        spacing: 2
        y: 20
        z: 2
        height: 40
        clip: false

        Label {
            height: 40
            width: 80
            text: musicControlMin.mediaTime + "/" + musicControlMin.formatTime(musicControlMin.currentDuration)
            font.bold: false
            font.pixelSize: 14
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            color: Style.themes.textColor
        }
        SButton {
            iconCharacter: "\uf0b6"
            width: 40
            height: 40
            radius: 40
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticon + 1
            shadowEnabled: false
            onClicked: {
                if(playerInfoDialog.visible) {
                    playerInfoDialog.close();
                } else {
                    playerInfoDialog.open();
                }
            }
            QTip {
                visible: parent.hovered
                text: "音乐详情"
            }
        }
        SButton {
            iconCharacter: "\uf043"
            width: 40
            height: 40
            radius: 40
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticon + 2
            shadowEnabled: false
            onHoveredChanged: {
                if(hovered) {
                    volumeControl.delay = 360
                    volumeControl.open();
                } else {
                    if(!volumeControl.visible) {
                        volumeControl.close();
                    }
                }
            }
            onClicked: {
                if(volumeControl.visible) {
                    volumeControl.close();
                } else {
                    volumeControl.delay = 0
                    volumeControl.open();
                }
            }
        }
        SButton {
            iconCharacter: "\uf0b2"
            width: 40
            height: 40
            radius: 40
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            shadowEnabled: false
            iconSize: Style.settings.texticon + 1
            onClicked: {
                if(desktopPlayer.visible) {
                    desktopPlayer.close()
                } else {
                    desktopPlayer.open()
                }
            }
            QTip {
                visible: parent.hovered
                text: "桌面部件"
            }
        }
        SButton {
            iconCharacter: "\uf098"
            width: 40
            height: 40
            radius: 40
            buttonColor: "transparent"
            hoverColor: Style.themes.hoverColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticon + 2
            shadowEnabled: false
            onClicked: {
                if(playList.visible) {
                    playList.close();
                } else {
                    playList.open();
                }
            }
            QTip {
                visible: parent.hovered
                text: "播放列表"
            }
        }
    }

    MouseArea {
        z: 0
        anchors.fill: parent
        onClicked: {
            if(mainLayout.state === "") {
                controlMaxLoader.active = true;
            } else {
                window.playermined();
                minedAnimation.start();
                mainLayout.state = "";
            }
        }
    }

    // 上一首
    function lastMedia() {
        if (window.sourceLyricsMode && !window.securePlaybackCurrent)
            return;
        const index = musicControlMin.securePlaybackActive ? playbackCoordinator.currentIndex : playListModel.playListIndex;
        if(index > 0) {
            if (musicControlMin.securePlaybackActive) {
                playbackCoordinator.playQueueEntry(index - 1);
                return;
            }
            playListModel.playListIndex -= 1;
            window.playQueueEntry(playListModel.playListIndex);
            if(windowsSmtc.available)
                windowsSmtc.setControlsEnabled(true, true,
                    playListModel.playListIndex < playListModel.count - 1,
                    playListModel.playListIndex > 0);
        }
    }
    // 下一首
    function enterMedia() {
        if (window.sourceLyricsMode && !window.securePlaybackCurrent)
            return;
        if (musicControlMin.securePlaybackActive) {
            const queueSize = playbackCoordinator.queue.length;
            const index = playbackCoordinator.currentIndex;
            if (queueSize > 0)
                playbackCoordinator.playQueueEntry(index < queueSize - 1 ? index + 1 : 0);
            return;
        }
        if(playListModel.playListIndex < playListModel.count - 1) {
            playListModel.playListIndex += 1;
        } else {
            playListModel.playListIndex = 0;
        }
        window.playQueueEntry(playListModel.playListIndex);
        if(windowsSmtc.available)
            windowsSmtc.setControlsEnabled(true, true,
                playListModel.playListIndex < playListModel.count - 1,
                playListModel.playListIndex > 0);
    }
    // 随机播放音乐
    function randomMedia() {
        if (window.sourceLyricsMode && !window.securePlaybackCurrent)
            return;
        if (musicControlMin.securePlaybackActive) {
            const queueSize = playbackCoordinator.queue.length;
            if (queueSize > 0)
                playbackCoordinator.playQueueEntry(Math.floor(Math.random() * queueSize));
            return;
        }
        playListModel.playListIndex = Math.floor( Math.random() * playListModel.count );
        window.playQueueEntry(playListModel.playListIndex);
        if(windowsSmtc.available)
            windowsSmtc.setControlsEnabled(true, true,
                playListModel.playListIndex < playListModel.count - 1,
                playListModel.playListIndex > 0);
    }
    // 切换播放列表显示
    function togglePlayList() {
        if(playList.visible) {
            playList.close();
        } else {
            playList.open();
        }
    }

    // 刷新音乐播放数据
    function refreshMusicPlay() {
        window.playQueueEntry(playListModel.playListIndex);
    }

    function refreshLegacyMusicPlay() {
        var source = playListModel.get(playListModel.playListIndex).source;
        if(source == -1) {
            mainWarn.tiped("旧本地队列歌曲已停止直播放；请在本地音乐插件中导入目录并重新加入队列", 0);
            return;
        } else {
            mainMedia.urlLocal = false;
            var sourcePath = playListModel.get(playListModel.playListIndex).path;
            MusicApi.getMusicInfo(sourcePath,0,source);
        }
    }

    ToolTip {
        id: volumeControl
        margins: 0
        parent: Overlay.overlay
        width: 180
        height: 40
        verticalPadding: 5
        leftPadding: 10
        rightPadding: 40
        delay: 360
        closePolicy: Popup.CloseOnPressOutside
        x: parent.width - 230
        y: parent.height - 110
        background: QBlurCard {
            anchors.fill: parent
            clip: false
            blurMax: 48
            borderRadius: 23
            blurSource: mainLayout
            shadowEffect: true
            rectXy: Qt.rect(volumeControl.x, volumeControl.y, 180, 40)
            //color: Style.themes.primaryBlurColor
        }
        contentItem: QSlider {
            z: 1
            to: 100
            implicitWidth: 130
            implicitHeight: 36
            valueText: Math.floor(value)
            value: Options.settings.musicVolume * 100
            onMoved: {
                Options.settings.musicVolume = value / 100
            }
        }
        enter: Transition {
            NumberAnimation { property: "y"; duration: 320; from: volumeControl.parent.height - 88; to: volumeControl.parent.height - 110; easing.type: Easing.OutExpo }
            NumberAnimation { property: "opacity"; duration: 320; from: 0; to: 1; easing.type: Easing.OutExpo }
        }
        exit: Transition {
            NumberAnimation { property: "y"; duration: 160; to: volumeControl.parent.height - 88; easing.type: Easing.InCubic }
            NumberAnimation { property: "opacity"; duration: 160; to: 0; easing.type: Easing.InCubic }
        }
    }

    PlayList {
        id: playList
        model: playListModel
        secureMode: window.securePlaybackActive || playList.showRestoredQueue
        secureAvailable: window.secureQueueAvailable
        secureModel: playbackCoordinator ? playbackCoordinator.queue : []
    }

    QOptionDialog {
        id: playerOptionDialog
        title: "播放器选项"
        dialogContentHeight: 430
        options: Column {
            width: parent.width
            spacing: 16

            SettingItem {
                label: "播放倍速"
                controlWidth: 120
                width: parent.width
                QDrop {
                    height: 36; width: 120
                    anchors.right: parent.right
                    choice: musicControlMin.playerRateIndex
                    model: ["0.5x","0.75x","1x-默认","1.25x","1.5x","2x","自定义"]
                    onTransformed: (choiced) => {
                        if (window.sourceLyricsMode)
                            playbackAdapter.setPlaybackRate([0.5,0.75,1.0,1.25,1.5,2.0,1.0][choiced]);
                        else
                            mainMedia.playbackRate = [0.5,0.75,1.0,1.25,1.5,2.0,1.0][choiced];
                        musicControlMin.playerRateIndex = choiced;
                    }
                }
            }

            SettingItem {
                label: "播放倍速调节"
                controlWidth: 120
                width: parent.width
                opacity: musicControlMin.playerRateIndex === 6 ? 1 : 0.5
                QSlider {
                    height: 36; width: 160
                    anchors.right: parent.right
                    from: 0.1
                    to: 4.0
                    stepSize: 0.1
                    leftText: true
                    valueText: value.toFixed(1)
                    value: musicControlMin.currentPlaybackRate
                    onMoved: {
                        if(musicControlMin.playerRateIndex === 6) {
                            if (window.sourceLyricsMode)
                                playbackAdapter.setPlaybackRate(value);
                            else
                                mainMedia.playbackRate = value;
                        }
                    }
                }
            }

            SettingItem {
                label: "切换音乐自动播放"
                controlWidth: 120
                width: parent.width
                QSwitch {
                    height: 36; width: 120
                    anchors.right: parent.right
                    switchTrue: mainMedia.autoPlay
                    onToggled: mainMedia.autoPlay = !mainMedia.autoPlay
                }
            }

            SettingItem {
                label: "启用间距补偿"
                controlWidth: 120
                width: parent.width
                QSwitch {
                    height: 36; width: 120
                    anchors.right: parent.right
                    switchTrue: false
                    onToggled: switchTrue = !switchTrue
                }
            }

            SettingItem {
                label: "音质"
                controlWidth: 120
                width: parent.width
                QDrop {
                    height: 36; width: 160
                    anchors.right: parent.right
                    choice: Options.settings.soundQuality
                    model: ["标准-144k","高清-320k","无损-500+k"]
                    onTransformed: (choiced) => {
                        Options.settings.soundQuality = choiced
                    }
                }
            }

            SettingItem {
                label: "使用默认输出设备"
                controlWidth: 120
                width: parent.width
                QSwitch {
                    height: 36; width: 120
                    anchors.right: parent.right
                    switchTrue: Options.settings.useDefaultDevice
                    onToggled: Options.settings.useDefaultDevice = !Options.settings.useDefaultDevice
                }
            }

            SettingItem {
                label: "自定输出设备"
                controlWidth: 120
                width: parent.width
                opacity: Options.settings.useDefaultDevice ? 0.5 : 1
                QDrop {
                    height: 36; width: 160
                    anchors.right: parent.right
                    choice: Options.settings.audioDevice
                    model: musicDevices.audioOutputs
                    useId: true
                    onTransformed: (choiced) => {
                        Options.settings.audioDevice = choiced
                    }
                }
            }

            SettingItem {
                label: "均衡器"
                controlWidth: 120
                width: parent.width
                QButton {
                    height: 36; width: 120
                    radius: Style.settings.labelRadius
                    anchors.right: parent.right
                    text: "默认"
                    shadowEnabled: false
                    //onClicked: FileDialog.open()
                }
            }
        }
    }

    QOptionDialog {
        id: playerInfoDialog
        objectName: "currentPlaybackInfoDialog"
        title: "音乐详情"
        dialogContentHeight: 370
        options: Column {
            width: parent.width
            spacing: 16
            SettingItem {
                label: window.sourceLyricsMode ? "来源：" : "文件名："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: (window.sourceLyricsMode ? window.lyricsAdapter.details.sourceLabel : window.lyricsAdapter.details.fileName) || "未知"
                    color: Style.themes.textColor
                    verticalAlignment: Text.AlignVCenter
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                }
            }
            SettingItem {
                label: "歌曲名："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: window.lyricsAdapter.details.title || "未知"
                    color: Style.themes.textColor
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
            SettingItem {
                label: "艺术家："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: window.lyricsAdapter.details.artist || "未知"
                    color: Style.themes.textColor
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
            SettingItem {
                label: "专辑："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: window.lyricsAdapter.details.album || "未知"
                    color: Style.themes.textColor
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
            SettingItem {
                label: "音频长度："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: musicControlMin.currentDuration > 0 ? musicControlMin.formatTime(musicControlMin.currentDuration) : "未知"
                    color: Style.themes.textColor
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
            SettingItem {
                label: "日期："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: window.lyricsAdapter.details.date || "未知"
                    color: Style.themes.textColor
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
            SettingItem {
                label: "音频格式："
                controlWidth: 120
                width: parent.width
                TextInput {
                    height: 36
                    anchors.right: parent.right
                    font.pixelSize: Style.settings.textmain
                    text: window.lyricsAdapter.details.format || "未知"
                    color: Style.themes.textColor
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Style.themes.themeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
