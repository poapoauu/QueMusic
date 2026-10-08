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
    readonly property var artistModel: musicAdapter ? musicAdapter.categoryArtists || null : null
    readonly property var playlistModel: musicAdapter ? musicAdapter.categoryPlaylists || null : null
    readonly property var chartModel: musicAdapter ? musicAdapter.categoryCharts || null : null
    readonly property string categoryStatusMessage: {
        if (!musicAdapter) return ""
        if (musicAdapter.categoryState === "loading") return "正在加载…"
        if (musicAdapter.categoryHasError)
            return musicAdapter.categoryState === "ready" ? "部分内容加载失败" : "加载失败"
        return ""
    }
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
    function requestCategorySections(model, retry, fallbackSection) {
        if (!musicAdapter || !model || musicAdapter.categoryState === "loading") return
        const action = retry ? "retry" : "loadMore"
        if (typeof musicAdapter[action] !== "function") return
        const ids = retry ? model.retrySectionIds : model.paginationSectionIds
        // Older Host models can still use the single-section contract.
        const targets = ids !== undefined ? Array.from(ids)
                       : !model.loadingMore && (retry || model.hasMore) ? [fallbackSection] : []
        for (const id of targets)
            musicAdapter[action](1, id)
    }
    function categoryPaginationBlocked(model) {
        return model && model.loadingMore === true
                && (!model.paginationSectionIds || model.paginationSectionIds.length === 0)
    }
    function categoryCanLoadMore(model) {
        return musicAdapter && musicAdapter.categoryState !== "loading" && model
                && model.paginationSectionIds && model.paginationSectionIds.length > 0
    }
    function categoryCanRetry(model) {
        return musicAdapter && musicAdapter.categoryState !== "loading" && model
                && model.retrySectionIds && model.retrySectionIds.length > 0
    }
    function categoryModelHasError(model) {
        return model && model.error && Object.keys(model.error).length > 0
    }
    function categoryModelIsEmpty(model) {
        return (!model || model.count === 0) && (!musicAdapter || musicAdapter.categoryState !== "loading")
                && (!model || !model.loadingMore) && !categoryModelHasError(model)
    }
    function categoryRows(model) {
        const rows = []
        if (model) for (let i = 0; i < model.count; ++i) rows.push(model.get(i))
        return rows
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
        return categoryRows(playlistModel)
    }
    function categoryArtists() {
        return categoryRows(artistModel)
    }
    function browseArtist(index) {
        return browsePresentation(categoryArtists()[index])
    }
    function categoryCharts() {
        return categoryRows(chartModel)
    }
    function browseChart(index) {
        return browsePresentation(categoryCharts()[index])
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
    function backCategoryDetail() {
        if (!musicAdapter || typeof musicAdapter.categoryBack !== "function"
                || !musicAdapter.categoryBack()) return false
        if (musicAdapter.categoryCanNavigateBack) {
            adapterDetailWindow.title = musicAdapter.categoryTitle || ""
            adapterDetailWindow.image = musicAdapter.categoryCover || ""
        } else {
            adapterDetailWindow.closed()
        }
        return true
    }
    Component.onCompleted: {
        if (musicAdapter)
            musicAdapter.activatePage(1)
    }
    Text {
        objectName: "categoryStatusText"
        x: 24; y: 116
        width: parent.width - 200
        text: playlistPage.categoryStatusMessage
        visible: !adapterDetailWindow.visible
        color: Style.themes.textColor
        font.pixelSize: Style.settings.textTip
    }
    QButton {
        objectName: "categoryRetryButton"
        x: parent.width - 144; y: 112
        width: 120; height: 28
        text: "重试"
        visible: !adapterDetailWindow.visible && musicAdapter && musicAdapter.categoryHasError === true
        enabled: musicAdapter && musicAdapter.categoryState !== "loading"
        onClicked: musicAdapter.refreshPage(1)
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
                if (musicAdapter)
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
                model: musicAdapter && musicAdapter.categorySongs ? musicAdapter.categorySongs : null
                clip: true
                menuModel: []
                toolText0: ""
                toolText1: ""
                toolText0ForRow: musicAdapter ? function(index) {
                    const row = searchSong.model && searchSong.model.get(index)
                    return row && musicAdapter.capabilities(row).canEnqueue ? "\uf095" : ""
                } : null
                sectionId: model && model.sectionId ? model.sectionId : ""
                hasMore: model && model.hasMore === true
                loadingMore: playlistPage.categoryPaginationBlocked(model)
                useLegacyLoadingState: false
                sectionError: model && model.error && Object.keys(model.error).length > 0 ? ({failed: true}) : ({})
                retryAction: function(section) {
                    playlistPage.requestCategorySections(searchSong.model, true, section)
                }
                isEnd: count > 0 && !hasMore && !loadingMore
                onEnded: {
                    playlistPage.requestCategorySections(model, false, sectionId)
                }
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
            Text {
                objectName: "categorySongsEmptyState"
                anchors.centerIn: parent
                visible: searchSong.count === 0 && playlistPage.categoryStatusMessage === ""
                text: "暂无歌曲"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.text
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
                model: playlistPage.playlistModel
                menuModel: []
                toolText0: ""
                toolText1: ""
                toolText1ForRow: function(index) {
                    const row = model && model.get(index)
                    return musicAdapter && row && musicAdapter.capabilities(row).canFavorite ? "\uf0c8" : ""
                }
                artistX: width / 2 - 50
                bottomMargin: 24
                sectionId: model && model.sectionId ? model.sectionId : ""
                hasMore: model && model.hasMore === true
                loadingMore: playlistPage.categoryPaginationBlocked(model)
                useLegacyLoadingState: false
                sectionError: playlistPage.categoryModelHasError(model) ? ({failed: true}) : ({})
                retryAction: function(section) { playlistPage.requestCategorySections(musicMenuList.model, true, section) }
                isEnd: count > 0 && !hasMore && !loadingMore
                onEnded: playlistPage.requestCategorySections(model, false, sectionId)
                onClicked: (index) => {
                    const row = model && model.get(index)
                    if (row) playlistPage.browsePresentation(row)
                }
                onToolClicked: (index, tool) => {
                    const row = model && model.get(index)
                    if (musicAdapter && row && tool === 1
                            && musicAdapter.capabilities(row).canFavorite)
                        musicAdapter.setFavorite(row, true)
                }
            }
            Text {
                objectName: "categoryPlaylistsEmptyState"
                anchors.centerIn: parent
                visible: playlistPage.categoryModelIsEmpty(playlistPage.playlistModel)
                text: "暂无歌单"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.text
            }
            QButton {
                objectName: "categoryPlaylistsMoreButton"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: 120; height: 40; radius: 20
                visible: musicMenuList.count === 0 && playlistPage.playlistModel
                         && (playlistPage.playlistModel.hasMore || playlistPage.playlistModel.loadingMore)
                text: playlistPage.playlistModel && playlistPage.playlistModel.loadingMore ? "正在加载…" : "更多"
                enabled: playlistPage.categoryCanLoadMore(playlistPage.playlistModel)
                onClicked: playlistPage.requestCategorySections(playlistPage.playlistModel, false, "")
            }
        }
        Item {
            id: cloud
            visible: false
            width: playlistChildPage.width
            height: playlistChildPage.height
            Text {
                objectName: "categoryChartsEmptyState"
                anchors.centerIn: parent
                visible: playlistPage.categoryModelIsEmpty(playlistPage.chartModel)
                text: "暂无排行榜"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.text
            }
            QScrollView {
                id: toplistFlick
                width: parent.width + 24
                height: parent.height - 48
                contentChildren: Flow {
                    id: toplistFlow
                    width: parent.width
                    padding: 12
                    topPadding: 68
                    bottomPadding: 24
                    //height: implicitHeight + 640
                    spacing: 20
                    Repeater {
                        objectName: "categoryChartCards"
                        model: playlistPage.categoryCharts()
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
                                source: modelData.cover || "qrc:/QueMusic/resources/app/musicpic.png"
                                radius: Style.settings.labelRadius
                                radius3: 0
                                radius4: 0
                                sourceSize: Qt.size(128,128)
                            }
                            // 插件明确标记的榜单
                            Rectangle {
                                x: 8
                                y: 8
                                width: 50
                                height: 20
                                radius: 10
                                color: Style.themes.primaryColor
                                Text {
                                    anchors.centerIn: parent
                                    text: "榜单"
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: Style.themes.themeColor
                                }
                            }
                            Text {
                                x: 12
                                y: 166
                                width: 132
                                text: modelData.title
                                font.bold: true
                                color: Style.themes.fontColor
                                font.pixelSize: Style.settings.textmain
                                elide: Text.ElideRight
                            }
                            Text {
                                x: 12
                                y: 190
                                width: 132
                                text: modelData.artist || ""
                                color: Style.themes.textColor
                                font.pixelSize: Style.settings.text
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: toplistCardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    playlistPage.browseChart(index)
                                }
                            }
                        }
                    }
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                spacing: 12
                QButton {
                    objectName: "categoryChartsMoreButton"
                    height: 40; width: 120; radius: 20
                    text: playlistPage.chartModel && playlistPage.chartModel.loadingMore ? "正在加载…" : "更多"
                    visible: playlistPage.chartModel && (playlistPage.chartModel.hasMore || playlistPage.chartModel.loadingMore)
                    enabled: playlistPage.categoryCanLoadMore(playlistPage.chartModel)
                    onClicked: playlistPage.requestCategorySections(playlistPage.chartModel, false, "")
                }
                QButton {
                    objectName: "categoryChartsRetryButton"
                    height: 40; width: 120; radius: 20
                    text: "重试"
                    visible: playlistPage.categoryModelHasError(playlistPage.chartModel)
                    enabled: playlistPage.categoryCanRetry(playlistPage.chartModel)
                    onClicked: playlistPage.requestCategorySections(playlistPage.chartModel, true, "")
                }
            }
        }
        Item {
            id: album
            visible: false
            width: playlistChildPage.width
            height: playlistChildPage.height
            Text {
                objectName: "categoryArtistsEmptyState"
                anchors.centerIn: parent
                visible: playlistPage.categoryModelIsEmpty(playlistPage.artistModel)
                text: "暂无歌手"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.text
            }
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
                        Row {
                            anchors.centerIn: parent
                            spacing: 12
                            QButton {
                                objectName: "categoryArtistsMoreButton"
                                height: 40; width: 120; radius: 20
                                iconCharacter: "\uf0f8"
                                text: playlistPage.artistModel && playlistPage.artistModel.loadingMore ? "正在加载…" : "更多"
                                visible: playlistPage.artistModel && (playlistPage.artistModel.hasMore || playlistPage.artistModel.loadingMore)
                                enabled: playlistPage.categoryCanLoadMore(playlistPage.artistModel)
                                onClicked: playlistPage.requestCategorySections(playlistPage.artistModel, false, "")
                            }
                            QButton {
                                objectName: "categoryArtistsRetryButton"
                                height: 40; width: 120; radius: 20
                                text: "重试"
                                visible: playlistPage.categoryModelHasError(playlistPage.artistModel)
                                enabled: playlistPage.categoryCanRetry(playlistPage.artistModel)
                                onClicked: playlistPage.requestCategorySections(playlistPage.artistModel, true, "")
                            }
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
        content: Item {
            width: adapterDetailWindow.width
            height: adapterDetailWindow.height
            QButton {
                objectName: "categoryDetailBackButton"
                x: parent.width - 144
                y: 48
                width: 120
                height: 36
                radius: 18
                text: "返回上级"
                enabled: musicAdapter && musicAdapter.categoryCanNavigateBack === true
                onClicked: playlistPage.backCategoryDetail()
            }
            Text {
                objectName: "categoryDetailStatusText"
                x: 24; y: 112
                width: parent.width - 200
                text: playlistPage.categoryStatusMessage
                color: Style.themes.textColor
                font.pixelSize: Style.settings.textTip
            }
            QButton {
                objectName: "categoryDetailRetryButton"
                x: parent.width - 144; y: 108
                width: 120; height: 28
                text: "重试"
                visible: musicAdapter && musicAdapter.categoryHasError === true
                enabled: musicAdapter && musicAdapter.categoryState !== "loading"
                onClicked: musicAdapter.refreshPage(1)
            }
            QListView {
                id: adapterDetailList
                objectName: "adapterPlaylistDetailList"
                x: 24
                y: 144
                width: adapterDetailWindow.width - 32
                height: adapterDetailWindow.height - 144
                model: musicAdapter ? musicAdapter.categoryItems : null
                menuModel: []
                toolText0: "\uf095"
                toolText1: ""
                toolText0ForRow: function(index) {
                    const row = adapterDetailList.model && adapterDetailList.model.get(index)
                    return musicAdapter && row && musicAdapter.capabilities(row).canEnqueue ? "\uf095" : ""
                }
                sectionId: model && model.sectionId ? model.sectionId : ""
                hasMore: model && model.hasMore === true
                loadingMore: playlistPage.categoryPaginationBlocked(model)
                useLegacyLoadingState: false
                sectionError: model && model.error && Object.keys(model.error).length > 0 ? ({failed: true}) : ({})
                retryAction: function(section) {
                    playlistPage.requestCategorySections(adapterDetailList.model, true, section)
                }
                isEnd: count > 0 && !hasMore && !loadingMore
                clip: true
                topMargin: 8
                bottomMargin: 24
                onClicked: (index) => {
                    if (!musicAdapter || !model) return
                    const row = model.get(index)
                    const caps = musicAdapter.capabilities(row)
                    if (caps.canPlay)
                        musicAdapter.play(row)
                    else if (caps.canBrowse)
                        playlistPage.browsePresentation(row)
                }
                onToolClicked: (index, tool) => {
                    if (!musicAdapter || !model || tool !== 0) return
                    const row = model.get(index)
                    if (musicAdapter.capabilities(row).canEnqueue)
                        musicAdapter.enqueue(row)
                }
                onEnded: {
                    playlistPage.requestCategorySections(model, false, sectionId)
                }
            }
            Text {
                objectName: "categoryDetailEmptyState"
                anchors.centerIn: parent
                visible: adapterDetailList.count === 0 && playlistPage.categoryStatusMessage === ""
                text: "暂无内容"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.text
            }
        }
    }
}
