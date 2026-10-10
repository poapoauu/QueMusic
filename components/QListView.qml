// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Controls.Basic
import QueMusic 1.0

ListView {
    id: view
    topMargin: 6
    bottomMargin: 24
    rightMargin: 16
    property int scrollToY: view.contentY
    property list<string> headerModel: isList ? ["标题","创建者",showListCount ? "曲目" : "","操作"] : ["标题","歌手","时长","操作"]//text,x
    property list<string> menuModel: ["下载到本地","分享","歌曲信息"]
    property list<int> selectedIndices: []
    property bool isList: false
    property bool showListCount: true
    property int artistX: width / 2 - 32
    property int toolX: width - 210
    property string toolText0: "\uf095"
    property string toolText1: "\uf0c8"
    // Per-row adapters keep a mixed list from hiding an action supported by a
    // particular item. Returning an empty string hides that item's button.
    property var toolText0ForRow: null
    property var toolText1ForRow: null
    // Opt-in Host source paging. Legacy callers retain their existing defaults.
    property bool sourcePaging: false
    property var sourceAdapter: null
    property int sourcePageKind: -1
    property int sourceContextRevision: 0
    onSourceAdapterChanged: sourceContextRevision++
    onSourcePageKindChanged: sourceContextRevision++
    onSourcePagingChanged: sourceContextRevision++
    onModelChanged: sourceContextRevision++
    Connections {
        target: view.sourceAdapter
        ignoreUnknownSignals: true
        function onSelectedSourceInstanceIdChanged() { view.sourceContextRevision++ }
        function onDestroyed() { view.sourceContextRevision++ }
    }
    property string sectionId: sourcePaging ? sourceSectionId() : ""
    property bool hasMore: sourcePaging ? !!sourceStateValue("hasMore") : true
    property bool loadingMore: sourcePaging ? !!sourceStateValue("loadingMore")
        && (!model || model.paginationSectionIds === undefined || model.paginationSectionIds.length === 0) : false
    property bool useLegacyLoadingState: !sourcePaging
    property var sectionError: sourcePaging && sourceStateValue("error")
        && Object.keys(sourceStateValue("error")).length > 0 ? ({failed: true}) : ({})
    property var retryAction: sourcePaging ? function() { view.requestSourceSections(true) } : null
    property alias menu: menu
    property bool isEnd: sourcePaging && count > 0 && !hasMore && !loadingMore
    contentWidth: view.width - 16
    synchronousDrag: true
    reuseItems: true
    onDraggingChanged: view.scrollToY = view.contentY
    signal clicked(int index)
    signal menuClicked(int index,int choice)
    signal toolClicked(int index,int tool)//从右往左2（菜单)，1（喜欢），0（通用）
    signal ended()

    function sourceStateValue(key) {
        if (!model) return undefined
        if (model.paginationSectionIds !== undefined || model.count === 0) return model[key]
        return model.get(model.count - 1)[key]
    }
    function sourceSectionId() {
        if (!model) return ""
        if (model.paginationSectionIds !== undefined || model.count === 0) return model.sectionId || ""
        return model.get(model.count - 1).sectionId || model.sectionId || ""
    }
    function requestSourceSections(retry) {
        if (!sourcePaging || !sourceAdapter || !model || sourcePageKind < 0) return
        const adapter = sourceAdapter
        const contextModel = model
        const pageKind = sourcePageKind
        const scope = adapter.selectedSourceInstanceId
        const revision = sourceContextRevision
        const action = retry ? "retry" : "loadMore"
        if (typeof adapter[action] !== "function") return
        const ids = retry ? contextModel.retrySectionIds : contextModel.paginationSectionIds
        // Snapshot before requests synchronously change model state. An empty
        // aggregate is terminal, not permission to use the legacy fallback.
        const targets = ids !== undefined ? Array.from(ids)
                      : !loadingMore && (retry || hasMore) && sectionId ? [sectionId] : []
        for (const id of targets) {
            // The first callback may synchronously replace (or replace and
            // restore) the context. Remaining IDs belong only to this snapshot.
            if (!sourcePaging || sourceContextRevision !== revision || sourceAdapter !== adapter
                    || model !== contextModel || sourcePageKind !== pageKind
                    || adapter.selectedSourceInstanceId !== scope) return
            if (id) adapter[action](pageKind, id)
        }
    }
    onEnded: if (sourcePaging) requestSourceSections(false)

    function retrySection() {
        if (typeof view.retryAction === "function")
            view.retryAction(view.sectionId)
    }

    function rowToolText(rowIndex, tool) {
        if (tool === 0 && typeof view.toolText0ForRow === "function")
            return view.toolText0ForRow(rowIndex) || ""
        if (tool === 1 && typeof view.toolText1ForRow === "function")
            return view.toolText1ForRow(rowIndex) || ""
        return tool === 0 ? view.toolText0 : view.toolText1
    }

    onAtYEndChanged: {
        if (atYEnd && view.hasMore && !view.loadingMore
                && (!view.useLegacyLoadingState || !MusicApi.loadState)) ended();
    }
    footer: Item {
        width: view.width
        height: 32
        visible: view.isEnd || (view.sectionError && Object.keys(view.sectionError).length > 0)
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            text: view.sectionError && Object.keys(view.sectionError).length > 0
                  ? "加载失败，点击重试" : "没有更多了~"
            color: Style.themes.textColor
            font.pixelSize: Style.settings.text
        }
        MouseArea {
            objectName: "sectionRetryArea"
            anchors.fill: parent
            enabled: view.sectionError && Object.keys(view.sectionError).length > 0
                     && typeof view.retryAction === "function"
            onClicked: view.retrySection()
        }
    }
    Menu {
        id: menu
        title: "Menu"
        //parent: Overlay.overlay
        parent: Overlay.overlay
        property int index
        //closePolicy: Popup.CloseOnEscape

        background: QBlurCard {
            implicitWidth: 150
            implicitHeight: 40
            shadowEffect: true
            blurSource: mainLayout
            rectXy: Qt.rect(menu.x, menu.y, menu.width, menu.height)
            cardColor: Style.themes.blurSecondaryColor
            borderRadius: Style.settings.labelRadius
        }

        Instantiator {
            model: view.menuModel
            delegate: MenuItem {
                id: menuItem
                background: Rectangle {
                    implicitWidth: 146
                    implicitHeight: 36
                    x: 2
                    y: 2
                    radius: Style.settings.labelRadius - 2
                    width: menuItem.width - 4
                    height: menuItem.height - 4
                    color: menuItem.down || menuItem.highlighted ? Style.themes.hoverColor : "transparent"
                }
                text: modelData
                contentItem: Text {
                    text: menuItem.text
                    color: Style.themes.fontColor//使用项目主题文字色，深浅色主题下都可读
                    font.pixelSize: Style.settings.textmain
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 12
                    elide: Text.ElideRight//保证超长歌手名不会撑破菜单项
                    clip: true
                }
                onTriggered: view.menuClicked(menu.index,index)
            }
            onObjectAdded: (i, obj) => menu.insertItem(i, obj)
            onObjectRemoved: (i, obj) => menu.removeItem(obj)
        }


        enter: Transition {
            NumberAnimation { property: "opacity"; duration: 160; from: 0; to: 1 }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; duration: 120; to: 0 }
        }
    }

    ScrollBar.vertical: ScrollBar {
        id: viewBar
        parent: view
        anchors.top: view.top
        anchors.right: view.right
        //anchors.leftMargin: 8
        anchors.bottom: view.bottom
        onPressedChanged: {
            view.scrollToY = view.contentY
        }
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            view.scrollToY = Math.max( -32 - view.topMargin, Math.min( view.scrollToY - (event.angleDelta.y * 0.25 * Qt.application.styleHints.wheelScrollLines), view.contentHeight - view.height + view.bottomMargin));
            listViewAnime.running = false;
            listViewAnime.running = true;
            viewBar.active = true;
            event.accepted = true;
        }
    }
    SequentialAnimation {
        id: listViewAnime
        NumberAnimation {
            target: view
            property: "contentY"
            duration: 240
            to: view.scrollToY
            easing.type: Easing.OutCubic
        }
        ScriptAction {
            script: viewBar.active = false
        }
    }

    rebound: Transition {
        NumberAnimation {
            properties: "y"
            duration: 420
            easing.type: Easing.Bezier
            easing.bezierCurve: [ 0.16, 0.03, 0.00, 1.00, 1, 1 ]
        }
    }
    header: Item {
        width: view.width
        height: 36
        Text {
            x: 80
            height: 36
            text: view.headerModel[0]
            color: Style.themes.textColor
            font.pixelSize: Style.settings.textTip
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            x: view.artistX
            height: 36
            text: view.headerModel[1]
            color: Style.themes.textColor
            font.pixelSize: Style.settings.textTip
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            x: view.width - 76
            height: 36
            text: view.headerModel[2]
            color: Style.themes.textColor
            font.pixelSize: Style.settings.textTip
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        /*Text {
            x: view.toolX + 70
            height: 36
            text: view.headerModel[3]
            color: Style.themes.textColor
            font.pixelSize: Style.settings.textTip
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }*/
        Rectangle {
            width: parent.width - 16
            height: 1
            color: Style.themes.sideColor
            opacity: 0.5
            y: 35
        }
    }

    displaced: Transition {
        id: listDisplacedAnime
        SequentialAnimation {
            PauseAnimation {
                duration: (listDisplacedAnime.ViewTransition.index - listDisplacedAnime.ViewTransition.targetIndexes[0]) * 40
            }
            NumberAnimation {
                properties: "y"
                duration: 240
                easing.type: Easing.Bezier; easing.bezierCurve: [ 0.23, 0.06, 0.00, 1.00, 1, 1 ]
            }
        }
    }
    add: Transition {
        ParallelAnimation {
            NumberAnimation {
                properties: "x"
                from: 400
                to: 0
                duration: 350
                easing.type: Easing.OutExpo
            }
            NumberAnimation {
                properties: "opacity"
                from: 0
                to: 1
                duration: 350
                easing.type: Easing.OutExpo
            }
        }
    }

    delegate: Rectangle {
        id: listDel
        readonly property var displayRow: typeof modelData !== "undefined" ? modelData : model
        height: 60
        width: view.width - 16
        color: view.selectedIndices.indexOf(index) !== -1 ? Style.themes.containColor : "#00000000"
        radius: Style.settings.labelRadius

        //radius: Style.settings.labelRadius
        //color: index % 2 === 0 ? Style.themes.blurOverlayColor : "transparent"

        Rectangle {
            anchors.fill: parent
            radius: Style.settings.labelRadius
            color: Style.themes.hoverColor
            opacity: listArea.containsMouse ? 1 : 0
            z: 1
            Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }

        QPicture {
            y: 8
            x: 8
            z: 4
            width: 44
            height: 44
            radius: 10
            source: displayRow.cover ? displayRow.cover.replace("{size}","64") : "qrc:/QueMusic/resources/app/musicpic.png"
        }


        Text {
            id: title
            objectName: "sourceRowTitle"
            x: 80
            y: 16
            z: 3
            width: view.artistX - 110
            height: 28
            text: displayRow.title || "Unknown"
            textFormat: Text.PlainText
            color: Style.themes.fontColor
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            font.pixelSize: Style.settings.textmain
            verticalAlignment: Text.AlignVCenter
            visible: true
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        Rectangle {
            color: Style.themes.containColor
            x: title.implicitWidth > title.width ? title.width + 75 : title.implicitWidth + 85
            y: 20
            width: 32
            height: 18
            radius: 9
            visible: displayRow.paytype === 3
            Text {
                text: "VIP"
                anchors.centerIn: parent
                color: Style.themes.themeColor
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }
        Text {
            x: view.artistX
            y: 16
            z: 3
            width: view.artistX - 128
            height: 28
            text: displayRow.artist || "Unknown"
            textFormat: Text.PlainText
            color: Style.themes.textColor
            font.weight: Font.Normal
            elide: Text.ElideRight
            font.pixelSize: Style.settings.text
            verticalAlignment: Text.AlignVCenter
            visible: true
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        Text {
            x: view.width - 92
            objectName: "rowDurationText"
            y: 16
            z: 3
            width: 60
            height: 28
            visible: !view.isList || view.showListCount
            text: view.isList ? displayRow.duration + "首"
                             : Math.floor((displayRow.duration || 0) / 60) + ":"
                               + ("0" + ((displayRow.duration || 0) % 60)).slice(-2)
            color: Style.themes.textColor
            font.bold: false
            elide: Text.ElideRight
            font.pixelSize: Style.settings.text
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            Behavior on color { ColorAnimation { duration: 120 } }
        }

        MouseArea {
            id: listArea
            anchors.fill: parent
            hoverEnabled: true
            z: 5
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    onClicked: view.clicked(index);
                } else if (view.menuModel.length > 0) {
                    menu.index = index;
                    view.menu.popup();
                }
                forceActiveFocus();
            }

            // Keep the original tool slots when an unsupported action is hidden.
            Item {
                x: view.toolX
                width: 112
                y: 12
                height: 36
                opacity: listArea.containsMouse ? 1 : 0
                readonly property string tool0: view.rowToolText(index, 0)
                readonly property string tool1: view.rowToolText(index, 1)
                Behavior on opacity { NumberAnimation { duration: 160 } }
                SButton {
                    iconCharacter: "\uf050"
                    visible: view.menuModel.length > 0
                    enabled: view.menuModel.length > 0
                    width: 36
                    height: 36
                    radius: 36
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    shadowEnabled: false
                    onClicked: {
                        menu.index = index
                        view.menu.popup()
                    }
                }
                SButton {
                    x: 38
                    objectName: "tool1Button"
                    property int rowIndex: index
                    iconCharacter: parent.tool1
                    visible: parent.tool1.length > 0
                    enabled: parent.tool1.length > 0
                    width: 36
                    height: 36
                    radius: 36
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    shadowEnabled: false
                    onClicked: view.toolClicked(index,1)
                }
                SButton {
                    x: 76
                    objectName: "tool0Button"
                    property int rowIndex: index
                    iconCharacter: parent.tool0
                    visible: parent.tool0.length > 0
                    enabled: parent.tool0.length > 0
                    width: 36
                    height: 36
                    radius: 36
                    buttonColor: "transparent"
                    hoverColor: Style.themes.hoverColor
                    shadowEnabled: false
                    onClicked: view.toolClicked(index,0)
                }
            }
        }
    }
}
