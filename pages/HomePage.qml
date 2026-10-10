// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Effects
import 'qrc:/QueMusic/components'

Item {
    id: homePage
    property var musicAdapter: null
    property var playbackAdapter: null
    property bool viewReady: false
    property bool changingDiscoveryContext: false
    onMusicAdapterChanged: {
        if (viewReady) resetSourceDiscoveryViews()
    }
    readonly property var playlistModel: musicAdapter ? musicAdapter.categoryPlaylists || null : null
    readonly property bool playlistHasError: playlistModel && playlistModel.error
            && Object.keys(playlistModel.error).length > 0
    readonly property string playlistStatusMessage: {
        if (musicAdapter && musicAdapter.categoryState === "loading") return "正在加载音源歌单…"
        if (playlistHasError) return playlistModel.count > 0 ? "部分音源歌单未能加载，可重试" : "音源歌单加载失败，可重试"
        return !playlistModel || playlistModel.count === 0 ? "当前范围暂无可用的音源歌单" : ""
    }
    //property alias animatedWindow: animationWrapper
    property real toolsWindow: 0
    //property bool displaytop: flickable.contentY > 60 ? true : false
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
    function rowFor(view, index) {
        return view && view.model && typeof view.model.get === "function"
                && index >= 0 && index < view.model.count ? view.model.get(index) : null
    }
    function playLatest() {
        if (queueHistoryStore.latest.replayable && queueHistoryStore.playLatest())
            return true
        mainWarn.tiped("这条播放记录已不在可用队列中，请从来源重新添加", 0)
        return false
    }
    function categoryStripItems() {
        if (!musicAdapter) return []
        const model = musicAdapter.categoryItems
        if (!model || typeof model.get !== "function" || model.count === undefined)
            return []
        const items = []
        for (let i = 0; i < model.count; ++i) {
            const row = model.get(i)
            if (row.entityType === 2 || row.entityType === 4)
                items.push(row)
        }
        return items
    }
    function browseCategory(index) {
        const row = categoryStripItems()[index]
        return browsePresentation(row)
    }
    function browsePresentation(row) {
        if (!row || !musicAdapter || !capabilitiesFor(row).canBrowse || !musicAdapter.browse(row))
            return false
        recommendWindow.opened(row.title || "", row.cover || "qrc:/QueMusic/resources/app/musicpic.png")
        window.exitIndex = 1
        return true
    }
    function browsePlaylist(index) {
        if (!playlistModel || typeof playlistModel.get !== "function"
                || !Number.isInteger(index) || index < 0 || index >= playlistModel.count)
            return false
        return browsePresentation(playlistModel.get(index))
    }
    function requestPlaylistSections(retry) {
        if (!musicAdapter || !playlistModel || musicAdapter.categoryState === "loading"
                || musicAdapter.categoryCanNavigateBack === true) return
        const action = retry ? "retry" : "loadMore"
        if (typeof musicAdapter[action] !== "function") return
        const targets = retry ? playlistModel.retrySectionIds : playlistModel.paginationSectionIds
        if (!targets) return
        for (const section of Array.from(targets)) musicAdapter[action](1, section)
    }
    function discoveryState(kind) {
        if (!musicAdapter) return "unsupported"
        return (kind === 0 ? musicAdapter.personalRadioState : musicAdapter.personalRadarState) || "unsupported"
    }
    function openPersonalDiscovery(kind) {
        if ((kind !== 0 && kind !== 1) || !musicAdapter
                || typeof musicAdapter.refreshDiscovery !== "function") return false
        const adapter = musicAdapter
        const scope = adapter.selectedSourceInstanceId
        if (personalWindow.visible && personalWindow.discoveryKind === kind && discoveryState(kind) === "loading")
            return false
        if (personalWindow.visible && personalWindow.discoveryKind !== kind
                && typeof musicAdapter.closeDiscovery === "function")
            musicAdapter.closeDiscovery(personalWindow.discoveryKind)
        if (musicAdapter !== adapter || adapter.selectedSourceInstanceId !== scope) return false
        personalWindow.discoveryKind = kind
        personalWindow.opening = true
        window.exitIndex = 1
        personalWindow.opened(kind === 0 ? "私人漫游" : "私人雷达", "qrc:/QueMusic/resources/app/rainbowMusicIcon.png")
        if (musicAdapter !== adapter || adapter.selectedSourceInstanceId !== scope || !personalWindow.visible) {
            personalWindow.opening = false
            return false
        }
        adapter.refreshDiscovery(kind)
        personalWindow.opening = false
        return musicAdapter === adapter && adapter.selectedSourceInstanceId === scope && personalWindow.visible
    }
    function requestPersonalSections(retry) {
        const adapter = musicAdapter
        const kind = personalWindow.discoveryKind
        const model = personalWindow.currentModel
        if (!personalWindow.visible || personalWindow.opening || !adapter || !model || discoveryState(kind) === "loading") return
        const action = retry ? "retryDiscovery" : "loadMoreDiscovery"
        if (typeof adapter[action] !== "function") return
        const targets = retry ? model.retrySectionIds : model.paginationSectionIds
        if (!targets) return
        const scope = adapter.selectedSourceInstanceId
        for (const section of Array.from(targets)) {
            if (musicAdapter !== adapter || personalWindow.discoveryKind !== kind
                    || personalWindow.currentModel !== model || !personalWindow.visible
                    || adapter.selectedSourceInstanceId !== scope) return
            if (section) adapter[action](kind, section)
        }
    }
    function resetSourceDiscoveryViews() {
        changingDiscoveryContext = true
        dailyRecomWindow.resetView()
        recommendWindow.resetView()
        personalWindow.resetView()
        changingDiscoveryContext = false
    }
    Connections {
        target: homePage.musicAdapter
        ignoreUnknownSignals: true
        function onSelectedSourceInstanceIdChanged() {
            const adapter = homePage.musicAdapter
            homePage.resetSourceDiscoveryViews()
            if (adapter && homePage.musicAdapter === adapter && typeof adapter.closeDiscovery === "function") {
                adapter.closeDiscovery(0)
                if (homePage.musicAdapter === adapter) adapter.closeDiscovery(1)
            }
        }
        function onCategoryNavigationChanged() {
            if (!recommendWindow.visible || homePage.changingDiscoveryContext) return
            if (homePage.musicAdapter && homePage.musicAdapter.categoryCanNavigateBack === true) {
                recommendWindow.title = homePage.musicAdapter.categoryTitle || ""
                recommendWindow.image = homePage.musicAdapter.categoryCover || ""
            } else {
                homePage.changingDiscoveryContext = true
                recommendWindow.resetView()
                homePage.changingDiscoveryContext = false
            }
        }
    }


    Component.onCompleted: {
        homePage.viewReady = true
        if (musicAdapter) {
            musicAdapter.activatePage(0)
            musicAdapter.activatePage(1)
            musicAdapter.activatePage(2)
        }
        const hour = new Date().getHours()
        if (hour > 3 && hour < 9) homeText.text = "早上好"
        else if (hour > 8 && hour < 13) homeText.text = "上午好"
        else if (hour > 12 && hour < 19) homeText.text = "下午好"
        else if (hour > 18 && hour < 23) homeText.text = "晚上好"
    }

    Item {
        id: homeMain
        anchors.fill: parent
        // 顶部标题
        Item {
            x: 24
            y: 24
            height: 40
            width: parent.width - 48
            z: 10
            Text {
                id: homeText
                x: 0
                y: 0
                height: 40
                verticalAlignment: Text.AlignVCenter
                text: "推荐"
                font.pixelSize: Style.settings.pageTitle
                font.weight: Font.DemiBold
                color: Style.themes.fontColor
            }
            //QButton { x: parent.width - 120; y: 0; height: 40; width: 120; iconCharacter: "\uf10c"; text: "刷新" }
            QDrop {
                objectName: "recommendationSourceScope"
                x: parent.width - 96
                y: 0
                height: 36; width: 120
                //radius: 18
                anchors.right: parent.right
                choice: homePage.sourceChoice()
                textColor: Style.themes.textColor
                color: Style.themes.primaryColor
                border.color: Style.themes.borderColor
                radius: 18
                cardRadius: Style.settings.labelRadius
                text: {
                    const option = homePage.sourceOptions()[choice]
                    return option ? option.displayName : ""
                }
                model: homePage.sourceOptions().map((option) => option.displayName)
                enabled: musicAdapter && homePage.sourceOptions().length > 0
                onTransformed: (choiced) => {
                    homePage.selectSource(choiced)
                }
            }
        }

        QScrollView {
            id: homeView
            x: 0
            y: 68
            width: homePage.width
            height: homePage.height - 68
            property int standWidth: homePage.width - 52

            contentChildren: Column {
                id: homeContent
                spacing: 16
                padding: 24
                Rectangle {
                    width: homeView.standWidth
                    height: warnText.implicitHeight + 40
                    color: Style.themes.containColor
                    radius: Style.settings.cubeRadius
                    border.color: Style.themes.sideColor
                    border.width: 1
                    Text {
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: iconFont.name
                        height: warnText.implicitHeight
                        text: "\uf11a"
                        color: Style.themes.themeColor
                        font.pixelSize: Style.settings.texticon
                    }
                    Text {
                        id: warnText
                        x: 48
                        y: 20
                        width: parent.width - 108
                        text: "该版本属于开发中Beta版本，是未正式发布的开发中测试版本，部分功能仍未有效，并且稳定性欠佳，非最终质量"
                        wrapMode: Text.Wrap
                        color: Style.themes.fontColor
                        font.bold: false
                        font.pixelSize: Style.settings.textmain
                    }
                    SButton {
                        iconCharacter: "\uf025"
                        x: parent.width - 52
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        height: 36
                        radius: 18
                        iconSize: Style.settings.texticon
                        buttonColor: "transparent"
                        shadowEnabled: false
                        onClicked: {
                            parent.visible = false;
                        }
                    }
                }
                // 首页头部部分
                Item {
                    height: 256
                    width: homeView.standWidth
                    readonly property int leftWidth: homeView.standWidth * 0.6
                    readonly property int rightWidth: homeView.standWidth * 0.4

                    // 每日推荐大卡片
                    QFloatCard {
                        width: parent.leftWidth - 8
                        height: 256

                        // 大标题
                        Text {
                            x: 16
                            y: 50
                            text: "DAILY RECOMMEND"
                            width: parent.width
                            font.pixelSize: Style.settings.textmain
                            font.bold: true
                            elide: Text.ElideRight
                            color: Style.themes.themeColor
                        }
                        Text {
                            x: 16
                            y: 88
                            text: "每日推荐"
                            width: parent.width
                            elide: Text.ElideRight
                            font.pixelSize: 32
                            font.bold: true
                            color: Style.themes.fontColor
                        }
                        Text {
                            x: 16
                            y: 152
                            text: "那些你反复循环的节奏，长成了今天的模样。"
                            width: parent.width
                            wrapMode: Text.Wrap
                            font.pixelSize: Style.settings.textmain
                            color: Style.themes.textColor
                        }
                        Rectangle {
                            x: parent.width - 152
                            y: 64
                            width: 128
                            height: 128
                            radius: Style.settings.cubeRadius
                            color: Style.themes.secondaryColor
                            QPicture {
                                anchors.fill: parent
                                anchors.margins: 8
                                sourceSize: Qt.size(128,128)
                                source: "qrc:/QueMusic/resources/app/rainbowMusicIcon.png"
                                radius: Style.settings.cubeRadius - 2
                            }

                            RectangularShadow {
                                anchors.fill: parent
                                z: -1
                                offset.x: 5
                                offset.y: 5
                                radius: Style.settings.cubeRadius
                                blur: 24
                                color: Style.themes.shadowColor
                            }
                        }

                        onClicked: {
                            if (musicAdapter)
                                musicAdapter.activatePage(0)
                            var image = "qrc:/QueMusic/resources/app/rainbowMusicIcon.png";
                            var title = "每日推荐";
                            dailyRecomWindow.opened(title,image);
                            window.exitIndex = 1;
                        }
                        controlItem: QButton {
                            x: 16
                            y: 192
                            height: 32
                            radius: 16
                            iconCharacter: "\uf0e7"
                            text: "前往查看"
                            shadowEnabled: false
                            buttonColor: Style.themes.themeColor
                            textColor: Style.themes.fullColor
                            iconColor: Style.themes.fullColor
                            onClicked: {
                                if (musicAdapter)
                                    musicAdapter.activatePage(0)
                                var image = "qrc:/QueMusic/resources/app/rainbowMusicIcon.png";
                                var title = "每日推荐";
                                dailyRecomWindow.opened(title,image);
                                window.exitIndex = 1;
                            }
                        }
                    }

                    QFloatCard {
                        x: parent.leftWidth + 8
                        width: parent.rightWidth - 8
                        height: 120

                        Text {
                            x: 16; y: 16
                            height: 20
                            text: "上次听到"
                            font.bold: true
                            font.pixelSize: Style.settings.textH2
                            color: Style.themes.fontColor
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !queueHistoryStore.latest.title
                            text: "还没有播放记录"
                            font.pixelSize: Style.settings.text
                            color: Style.themes.textColor
                        }
                        onClicked: homePage.playLatest()
                        controlItem: [
                            QPicture {
                                y: 56
                                x: 16
                                width: 52; height: 52
                                radius: 12
                                source: "qrc:/QueMusic/resources/app/musicpic.png"
                            },
                            Text {
                                x: 78
                                y: 60
                                width: parent.width - 78
                                height: 23
                                text: queueHistoryStore.latest.title || ""
                                font.bold: true
                                font.pixelSize: Style.settings.textmain
                                color: Style.themes.fontColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            },
                            Text {
                                x: 78
                                y: 83
                                width: parent.width - 78
                                height: 21
                                text: queueHistoryStore.latest.artist || ""
                                font.pixelSize: Style.settings.text
                                color: Style.themes.textColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            },
                            QButton {
                                x: parent.width - 84
                                y: 10
                                height: 32; width: 68
                                radius: 18
                                iconCharacter: "\uf00e"
                                text: "播放"
                                enabled: queueHistoryStore.latest.replayable === true
                                shadowEnabled: false
                                buttonColor: Style.themes.themeColor
                                textColor: Style.themes.fullColor
                                iconColor: Style.themes.fullColor
                                onClicked: homePage.playLatest()
                            },
                            SButton {
                                x: parent.width - 120
                                y: 10
                                width: 32
                                height: 32
                                radius: 16
                                iconCharacter: "\uf075"
                                shadowEnabled: false
                                buttonColor: Style.themes.sideColor
                                onClicked: {
                                }
                            },
                            //  这条记录已经在队列中时，提供队列入口；不从历史快照伪造入队动作。
                            SButton {
                                x: parent.width - 52
                                y: 64
                                iconCharacter: "\uf0c9"
                                visible: queueHistoryStore.latest.replayable === true
                                width: 36
                                height: 36
                                radius: 36
                                buttonColor: "transparent"
                                hoverColor: Style.themes.hoverColor
                                shadowEnabled: false
                                onClicked: window.togglePlayList()
                            }
                        ]
                    }

                    // 我的收藏歌单
                    QFloatCard {
                        x: parent.leftWidth + 8
                        y: 136
                        width: parent.rightWidth - 8
                        height: 120
                        color: Style.themes.containColor

                        Row {
                            anchors.centerIn: parent
                            spacing: 12
                            Text {
                                text: "\uf0c1"
                                font.family: iconFont.name
                                font.pixelSize: 32
                                color: Style.themes.themeColor
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Column {
                                spacing: 2
                                Text {
                                    text: "我的收藏歌单"
                                    font.bold: true
                                    font.pixelSize: Style.settings.textH1
                                    color: Style.themes.fontColor
                                }
                                Text {
                                    objectName: "homeFavoritePlaylistCount"
                                    text: musicAdapter && musicAdapter.favoriteLists
                                          ? "已加载 " + musicAdapter.favoriteLists.count + " 个歌单"
                                          : "已加载 0 个歌单"
                                    font.pixelSize: Style.settings.textmain
                                    color: Style.themes.textColor
                                }
                            }
                        }
                        onClicked: {
                            sidebar.indexed(3);
                            mainContent.contentIndexed(3);
                        }
                    }
                }

                QHead { text: "私人专属" }

                Item {
                    height: 180
                    width: homeView.standWidth
                    readonly property int leftWidth: homeView.standWidth * 0.5 - 8
                    readonly property int rightWidth: homeView.standWidth * 0.5 - 8
                    Rectangle {
                        x: 0
                        y: 0
                        width: parent.leftWidth
                        height: 180
                        color: Style.themes.primaryColor
                        radius: Style.settings.cubeRadius
                        Text {
                            x: 12
                            y: 16
                            height: 24
                            text: "歌单分类"
                            font.pixelSize: Style.settings.textH2
                            font.bold: true
                            color: Style.themes.fontColor
                            verticalAlignment: Text.AlignVCenter
                        }
                        RectangularShadow {
                            anchors.fill: parent
                            z: -1
                            offset.x: 3
                            offset.y: 5
                            radius: Style.settings.cubeRadius
                            blur: 10
                            spread: 0
                            color: Style.themes.shadowColor
                        }

                        ListView {
                            id: categoryList
                            objectName: "recommendationCategoryList"
                            y: 56
                            width: parent.width
                            height: 96
                            orientation: ListView.Horizontal
                            spacing: 20
                            leftMargin: 16
                            rightMargin: 16
                            clip: true
                            model: homePage.categoryStripItems()

                            // 隐藏系统滚动条，用惯性和鼠标拖拽
                            interactive: true
                            boundsBehavior: Flickable.DragOverBounds

                            delegate: Item {
                                id: catDel
                                width: 96
                                height: 96
                                property int radius: Style.settings.cubeRadius
                                //scale: catMouse.containsMouse ? 1.06 : 1.0
                                //Behavior on scale { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

                                QPicture {
                                    id: card
                                    anchors.fill: parent
                                    radius: catDel.radius
                                    source: (model.cover || "").replace("{size}", "256") || "qrc:/QueMusic/resources/app/musicpic.png"
                                    sourceSize: Qt.size(256,256)

                                    // 底部渐变遮罩 —— 保证标题永远可读
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 48
                                        radius: catDel.radius
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "transparent" }
                                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.72) }
                                        }
                                    }

                                    // 分类标题
                                    Text {
                                        y: 60
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: model.title || ""
                                        font.pixelSize: 16
                                        font.weight: Font.Bold
                                        color: "#FFFFFF"
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                    }

                                    // hover 时浮现的箭头
                                    Text {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 14
                                        text: "\uf0e7"
                                        font.family: iconFont.name
                                        font.pixelSize: 18
                                        color: "#FFFFFF"
                                        opacity: catMouse.containsMouse ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 200 } }
                                    }
                                }

                                MouseArea {
                                    id: catMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: catDel.scale = 0.96
                                    onReleased: catDel.scale = 1.0
                                    onCanceled: catDel.scale = 1.0

                                    onClicked: {
                                        homePage.browseCategory(index)
                                    }
                                }
                            }
                        }
                    }
                    QFloatCard {
                        x: parent.leftWidth + 16
                        y: 0
                        objectName: "homePersonalRadioCard"
                        width: parent.rightWidth
                        height: 82
                        Text {
                            x: 20
                            y: 20
                            width: 42
                            height: 42
                            text: "\uf104"
                            font.family: iconFont.name
                            font.pixelSize: 32
                            color: Style.themes.themeColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 70
                            y: 20
                            height: 22
                            text: "私人漫游"
                            color: Style.themes.fontColor
                            font.pixelSize: Style.settings.textmain
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 70
                            y: 42
                            height: 20
                            text: "来自音源账号的个性化推荐"
                            color: Style.themes.textColor
                            font.pixelSize: Style.settings.text
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            homePage.openPersonalDiscovery(0)
                        }
                        controlItem: SButton {
                            objectName: "homePersonalRadioOpen"
                            x: parent.width - 50
                            y: 23
                            iconCharacter: "\uf0e7"
                            width: 36
                            height: 36
                            radius: 18
                            buttonColor: "transparent"
                            shadowEnabled: false
                            onClicked: homePage.openPersonalDiscovery(0)
                        }
                    }
                    QFloatCard {
                        x: parent.leftWidth + 16
                        y: 98
                        objectName: "homePersonalRadarCard"
                        width: parent.rightWidth
                        height: 82
                        Text {
                            x: 20
                            y: 20
                            width: 42
                            height: 42
                            text: "\uf109"
                            font.family: iconFont.name
                            font.pixelSize: 32
                            color: Style.themes.themeColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 70
                            y: 20
                            height: 22
                            text: "私人雷达"
                            color: Style.themes.fontColor
                            font.pixelSize: Style.settings.textmain
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 70
                            y: 42
                            height: 20
                            text: "来自音源账号的私人发现"
                            color: Style.themes.textColor
                            font.pixelSize: Style.settings.text
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            homePage.openPersonalDiscovery(1)
                        }
                        controlItem: SButton {
                            objectName: "homePersonalRadarOpen"
                            x: parent.width - 50
                            y: 23
                            iconCharacter: "\uf0e7"
                            width: 36
                            height: 36
                            radius: 18
                            buttonColor: "transparent"
                            shadowEnabled: false
                            onClicked: homePage.openPersonalDiscovery(1)
                        }
                    }
                }


                QHead { text: "音源歌单" }
                Text {
                    objectName: "homePlaylistsStatus"
                    width: homeView.standWidth
                    text: homePage.playlistStatusMessage
                    textFormat: Text.PlainText
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    color: Style.themes.textColor
                    font.pixelSize: Style.settings.text
                }

                Flow {
                    spacing: 24
                    width: homeView.standWidth

                    Repeater {
                        objectName: "homePlaylistCards"
                        model: homePage.playlistModel
                        delegate: Rectangle {
                            width: 148
                            height: 256
                            radius: Style.settings.labelRadius
                            color: Style.themes.primaryColor
                            scale: hotPlayListsArea.containsMouse ? 1.05 : 1.0
                            Behavior on scale { NumberAnimation { duration: 240; easing.type: Easing.OutExpo } }
                            RectangularShadow {
                                anchors.fill: parent
                                z: -1
                                offset.x: 2
                                offset.y: 2
                                radius: Style.settings.labelRadius
                                blur: hotPlayListsArea.containsMouse ? 24 : 8
                                spread: 0
                                visible: true
                                color: Style.themes.shadowColor
                                Behavior on blur { NumberAnimation { duration: 240; easing.type: Easing.OutExpo } }
                            }
                            MouseArea {
                                id: hotPlayListsArea
                                objectName: "homePlaylistCardAction"
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: homePage.playlistModel
                                        && homePage.capabilitiesFor(homePage.playlistModel.get(index)).canBrowse === true
                                onClicked: homePage.browsePlaylist(index)
                            }
                            QPicture {
                                width: 148
                                height: 148
                                source: model.cover || "qrc:/QueMusic/resources/app/musicpic.png"
                                radius: Style.settings.labelRadius
                                //cache: true
                                sourceSize: Qt.size(128,128)
                                radius3: 0
                                radius4: 0
                            }
                            Text {
                                objectName: "homePlaylistCardTitle"
                                x: 16
                                y: 160
                                width: 116
                                text: model.title
                                textFormat: Text.PlainText
                                font.bold: true
                                color: Style.themes.fontColor
                                font.pixelSize: Style.settings.textmain
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                clip: true
                                elide: Text.ElideRight
                            }
                            Text {
                                x: 16
                                y: 200
                                width: 116
                                height: 18
                                text: model.artist || ""
                                textFormat: Text.PlainText
                                color: Style.themes.textColor
                                font.pixelSize: Style.settings.text
                                //wrapMode: Text.Wrap
                                //maximumLineCount: 2
                                clip: true
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                x: 0
                                y: 224
                                height: 32
                                width: 148
                                color: Style.themes.sideColor
                                bottomLeftRadius: Style.settings.labelRadius
                                bottomRightRadius: Style.settings.labelRadius
                                Text {
                                    id: playIcon
                                    x: 16
                                    height: 32
                                    text: "\uf0e7"
                                    font.pixelSize: Style.settings.text
                                    font.family: iconFont.name
                                    color: Style.themes.textColor
                                    verticalAlignment: Text.AlignVCenter
                                }
                                Text {
                                    x: 20 + playIcon.width
                                    height: 32
                                    text: hotPlayListsArea.enabled ? "查看曲目" : "暂不可浏览"
                                    font.pixelSize: Style.settings.textTip
                                    color: Style.themes.textColor
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }

                Item {
                    height: 60
                    width: homeView.standWidth
                    QButton {
                        objectName: "homePlaylistsMoreButton"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        height: 40; width: 120
                        radius: 20
                        iconCharacter: "\uf0f8"
                        text: "更多"
                        visible: homePage.playlistModel && homePage.playlistModel.paginationSectionIds
                                && homePage.playlistModel.paginationSectionIds.length > 0
                        enabled: musicAdapter && musicAdapter.categoryState !== "loading"
                                && musicAdapter.categoryCanNavigateBack !== true
                        onClicked: homePage.requestPlaylistSections(false)
                    }
                    QButton {
                        objectName: "homePlaylistsRetryButton"
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: homePage.playlistModel && homePage.playlistModel.paginationSectionIds
                                && homePage.playlistModel.paginationSectionIds.length > 0 ? 132 : 0
                        height: 40; width: 120
                        radius: 20
                        text: "重试"
                        visible: homePage.playlistHasError
                        enabled: musicAdapter && musicAdapter.categoryState !== "loading"
                                && musicAdapter.categoryCanNavigateBack !== true && homePage.playlistModel
                                && homePage.playlistModel.retrySectionIds && homePage.playlistModel.retrySectionIds.length > 0
                        onClicked: homePage.requestPlaylistSections(true)
                    }
                }
            }
        }
    }
    AnimatorWindow {
        id: dailyRecomWindow
        objectName: "dailyRecommendationWindow"
        mainTarget: homeMain
        haveControl: false
        content: Item {

            QListView {
                id: dailyRecomView
                objectName: "recommendationList"
                x: 24
                y: 128
                width: dailyRecomWindow.width - 32
                height: dailyRecomWindow.height - 128
                model: musicAdapter ? musicAdapter.recommendSongs : null
                clip: true
                //reuseItems: true
                topMargin: 8
                bottomMargin: 24
                sourcePaging: true
                sourceAdapter: musicAdapter
                sourcePageKind: 0
                menuModel: []
                toolText0: ""
                toolText1: ""
                toolText0ForRow: function(index) {
                    return homePage.capabilitiesFor(homePage.rowFor(dailyRecomView, index)).canEnqueue ? "\uf095" : ""
                }
                toolText1ForRow: function(index) {
                    return homePage.capabilitiesFor(homePage.rowFor(dailyRecomView, index)).canFavorite ? "\uf0c8" : ""
                }

                onClicked: (index) => {
                    const row = homePage.rowFor(dailyRecomView, index)
                    if (homePage.capabilitiesFor(row).canPlay)
                        musicAdapter.play(row)
                }

                onToolClicked: (index,tool) => {
                    const row = homePage.rowFor(dailyRecomView, index)
                    const caps = homePage.capabilitiesFor(row)
                    if (tool === 0 && caps.canEnqueue)
                        musicAdapter.enqueue(row)
                    else if (tool === 1 && caps.canFavorite)
                        musicAdapter.setFavorite(row, true)
                }

            }
        }
    }

    // 私人漫游 / 私人雷达
    AnimatorWindow {
        id: personalWindow
        objectName: "personalDiscoveryWindow"
        mainTarget: homeMain
        haveControl: false
        property int discoveryKind: 0
        property bool opening: false
        readonly property var currentModel: !musicAdapter ? null
            : discoveryKind === 0 ? musicAdapter.personalRadio || null : musicAdapter.personalRadar || null
        readonly property string discoveryState: homePage.discoveryState(discoveryKind)
        readonly property string statusMessage: {
            if (discoveryState === "loading") return "正在加载私人内容…"
            if (discoveryState === "unsupported") return "当前范围暂无支持此功能的音源"
            if (discoveryState === "forbidden") return "当前音源账号无权使用此功能，请在插件管理中检查"
            if (discoveryState === "failed") return "私人内容加载失败，可重试"
            return "当前范围暂无私人推荐曲目"
        }
        onVisibleChanged: {
            if (!visible && !homePage.changingDiscoveryContext && musicAdapter
                    && typeof musicAdapter.closeDiscovery === "function")
                musicAdapter.closeDiscovery(discoveryKind)
        }
        content: Item {
            QListView {
                id: personalView
                objectName: "homePersonalDiscoveryList"
                x: 24
                y: 128
                width: personalWindow.width - 32
                height: personalWindow.height - 128
                model: personalWindow.currentModel
                clip: true
                topMargin: 8
                bottomMargin: 24
                sourcePaging: true
                menuModel: []
                toolText0: ""
                toolText1: ""
                toolText0ForRow: function(index) {
                    return homePage.capabilitiesFor(homePage.rowFor(personalView, index)).canEnqueue ? "\uf095" : ""
                }
                toolText1ForRow: function(index) {
                    return homePage.capabilitiesFor(homePage.rowFor(personalView, index)).canFavorite ? "\uf0c8" : ""
                }
                retryAction: function() { homePage.requestPersonalSections(true) }

                onClicked: (index) => {
                    const row = homePage.rowFor(personalView, index)
                    if (homePage.capabilitiesFor(row).canPlay) musicAdapter.play(row)
                }

                onToolClicked: (index,tool) => {
                    const row = homePage.rowFor(personalView, index)
                    const caps = homePage.capabilitiesFor(row)
                    if (tool === 0 && caps.canEnqueue) musicAdapter.enqueue(row)
                    else if (tool === 1 && caps.canFavorite) musicAdapter.setFavorite(row, true)
                }

                onEnded: homePage.requestPersonalSections(false)
            }
            Text {
                objectName: "homePersonalDiscoveryStatus"
                x: 24; y: 168
                width: parent.width - 48
                text: personalWindow.statusMessage
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Style.themes.textColor
                font.pixelSize: Style.settings.text
                visible: personalView.count === 0
            }
            QButton {
                objectName: "homePersonalDiscoveryRetry"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 224; width: 120; height: 40; radius: 20
                text: "重试"
                visible: personalView.count === 0 && (personalWindow.discoveryState === "failed" || personalWindow.discoveryState === "forbidden")
                enabled: !!personalWindow.currentModel && personalWindow.currentModel.retrySectionIds
                    && personalWindow.currentModel.retrySectionIds.length > 0
                onClicked: homePage.requestPersonalSections(true)
            }
        }
    }

    AnimatorWindow {
        id: recommendWindow
        objectName: "recommendationDetailWindow"
        mainTarget: homeMain
        haveControl: false
        onVisibleChanged: {
            if (!visible && !homePage.changingDiscoveryContext
                    && musicAdapter && typeof musicAdapter.closeCategoryBrowse === "function")
                musicAdapter.closeCategoryBrowse()
        }
        content: QListView {
            id: recomView
            objectName: "homeCategoryDetailList"
            x: 24
            y: 128
            width: recommendWindow.width - 32
            height: recommendWindow.height - 128
            model: musicAdapter ? musicAdapter.categoryItems : null
            clip: true
            //reuseItems: true
            topMargin: 8
            bottomMargin: 24
            isList: true
            showListCount: false
            sourcePaging: true
            sourceAdapter: musicAdapter
            sourcePageKind: 1
            menuModel: []
            toolText0: ""
            toolText1: ""
            toolText0ForRow: function(index) {
                return homePage.capabilitiesFor(homePage.rowFor(recomView, index)).canEnqueue ? "\uf095" : ""
            }
            toolText1ForRow: function(index) {
                return homePage.capabilitiesFor(homePage.rowFor(recomView, index)).canFavorite ? "\uf0c8" : ""
            }

            onClicked: (index) => {
                const row = homePage.rowFor(recomView, index)
                const caps = homePage.capabilitiesFor(row)
                if (row && row.entityType === 0 && caps.canPlay)
                    musicAdapter.play(row)
                else if (row && caps.canBrowse && musicAdapter.browse(row)) {
                    recommendWindow.opened(row.title || "", row.cover || "qrc:/QueMusic/resources/app/musicpic.png")
                    window.exitIndex = 1
                }
            }
            onToolClicked: (index,tool) => {
                const row = homePage.rowFor(recomView, index)
                const caps = homePage.capabilitiesFor(row)
                if (tool === 0 && caps.canEnqueue) musicAdapter.enqueue(row)
                else if (tool === 1 && caps.canFavorite) musicAdapter.setFavorite(row, true)
            }
        }
    }
}
