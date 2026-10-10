// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import 'qrc:/QueMusic/components'

// 左侧边栏
Rectangle {
    z: 1
    id: sidebar
    objectName: "originalSidebar"
    width: 210
    property var musicAdapter: null
    property var playbackAdapter: null
    property var styleObject: null
    readonly property var styleSettings: styleObject ? styleObject.settings : fallbackSettings
    readonly property var styleThemes: styleObject ? styleObject.themes : fallbackThemes
    property color baseColor: "transparent"
    property color choiceColor: styleThemes.hoverColor
    property color choiceTextColor: styleThemes.fontColor
    property var contentController: null
    property var windowObject: null
    property string textFontFamily: ""
    property string iconFontFamily: ""
    property url iconSource: ""
    color: styleSettings.backmode === 0 ? (styleSettings.sidebarColor ? styleThemes.secondaryColor : styleThemes.primaryColor) : baseColor
    QtObject {
        id: fallbackSettings
        property int backmode: 0
        property bool sidebarColor: false
        property int sidebarStyle: 1
        property int labelRadius: 12
        property int texticon: 16
        property int textmain: 13
    }
    QtObject {
        id: fallbackThemes
        property color hoverColor: "#1a000000"
        property color fontColor: "#000000"
        property color themeColor: "#3481fa"
        property color primaryColor: "#fdfdff"
        property color secondaryColor: "#f3f4f8"
        property color textColor: "#333333"
    }
    //layer.enabled: true
    //layer.smooth: true
    Connections {
        target: sidebar.styleObject
        ignoreUnknownSignals: true
        function onChangeTheme() {
            if(sidebar.styleSettings.sidebarStyle === 0) {
                sidebar.choiceColor = sidebar.styleThemes.hoverColor;
                sidebar.choiceTextColor = sidebar.styleThemes.fontColor;
                choicebar.x = 18;
                choicebar.radius = 2;
            } else if(sidebar.styleSettings.sidebarStyle === 1) {
                sidebar.choiceColor = sidebar.styleThemes.themeColor;
                sidebar.choiceTextColor = sidebar.styleThemes.primaryColor;
                choicebar.x = 0;
                choicebar.radius = 0;
            }
        }
    }

    function indexed(choice) {
        choicebar.willBarY = choice > 2 ? 44 * choice + 68 : 44 * choice + 80;
        navlistview.choiceIndex = choice;
        if(choice > choicebar.indexOld) {
            downBar.stop();
            upBar.stop();
            downBar.running = true;
        } else if(choice < choicebar.indexOld) {
            upBar.stop();
            downBar.stop();
            upBar.running = true;
        } else if(windowObject) {
            windowObject.exit();
        }
        choicebar.indexOld = choice;
    }

    function contentIndexForNav(choice) {
        return choice;
    }

    function navigate(choice) {
        if(choice < 0 || choice >= navModel.count || choice === 2)
            return false;
        sidebar.indexed(choice);
        return contentController
                ? contentController.contentIndexed(sidebar.contentIndexForNav(choice))
                : true;
    }

    Connections {
        target: sidebar.windowObject
        function onExit() {
            if(sidebar.contentController
                    && sidebar.contentController.pageIndex === 6
                    && sidebar.windowObject.exitIndex <= 1)
                sidebar.contentController.contentIndexed(navlistview.choiceIndex)
        }
    }

    // 选择动画条
    Rectangle {
        id: choicebar
        x: 18
        width: 4
        height: barBottom - y//22
        radius: 2 //Style.settings.labelRadius
        topRightRadius: 2
        bottomRightRadius: 2
        color: sidebar.styleThemes.themeColor
        y: 80
        property int barBottom: 102
        property int willBarY: 80
        property int indexOld: 0

        ParallelAnimation {
            id: downBar
            NumberAnimation {
                property: "barBottom"
                target: choicebar
                //from: choicebar.barBottom
                to: choicebar.willBarY + 22
                easing.type: Easing.Bezier
                easing.bezierCurve: [ 0.50, 0.00, 0.00, 1.00, 1, 1 ]
                duration: 280
            }
            NumberAnimation {
                property: "y"
                target: choicebar
                //from: choicebar.y
                to: choicebar.willBarY
                easing.type: Easing.Bezier
                easing.bezierCurve: [ 1.00, 0.00, 0.50, 1.00, 1, 1 ]
                duration: 280
            }
        }
        ParallelAnimation {
            id: upBar
            NumberAnimation {
                property: "barBottom"
                target: choicebar
                //from: choicebar.barBottom
                to: choicebar.willBarY + 22
                easing.type: Easing.Bezier
                easing.bezierCurve: [ 1.00, 0.00, 0.50, 1.00, 1, 1 ]
                duration: 280
            }
            NumberAnimation {
                property: "y"
                target: choicebar
                //from: choicebar.y
                to: choicebar.willBarY
                easing.type: Easing.Bezier
                easing.bezierCurve: [ 0.50, 0.00, 0.00, 1.00, 1, 1 ]
                duration: 280
            }
        }
    }

    //分隔条
    Rectangle {
        x: 20
        width: 170
        height: 2
        color: Qt.rgba(0.6,0.6,0.6,0.3)
        y: 173
    }


    // Sidebar Header/Section Title
    Item {
        x: 0
        y: 0
        width: 200
        height: 60

        //icon
        Image {
            y: 18
            x: 25
            width: 24
            height: 24
            source: sidebar.iconSource
            sourceSize: Qt.size(24, 24)
        }

        // App Title
        Text {
            y: 21
            x: 61
            height: 18
            text: "QueMusic"
            font.family: sidebar.textFontFamily
            font.pixelSize: 16
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            color: sidebar.styleThemes.fontColor

        }

        Rectangle {
            x: 150
            y: 20
            width: 40
            height: 20
            color: sidebar.styleThemes.themeColor
            radius: 6
            Text {
                anchors.centerIn: parent
                text: "Beta"
                font.pixelSize: 12
                color: sidebar.styleThemes.primaryColor

            }
        }
    }

    // Navigation List
    ListModel {
        id: navModel
        ListElement { display: "推荐"; iconChar: "\uf0bf" } // tj
        ListElement { display: "分类"; iconChar: "\uf044" }  // fl
        ListElement { display: ""; iconChar: "" }     // empty
        ListElement { display: "收藏"; iconChar: "\uf0c1" }  // sc
        ListElement { display: "本地"; iconChar: "\uf0f5" }   // bd
        ListElement { display: "下载"; iconChar: "\uf00f" }   // xz
    }

    Column {
        id: navlistview
        x: 15
        y: 70
        width: 180
        height: sidebar.height - 78
        property int choiceIndex: 0

        spacing: 2
        z: 10
        Repeater {
            objectName: "sidebarNavigation"
            model: navModel

            delegate: Rectangle {
                id: navDelegate
                width: navlistview.width
                height: 42
                radius: sidebar.styleSettings.labelRadius
                Component.onCompleted: {
                    if (index === 2) height = 30
                }
                color: isSelected ? sidebar.choiceColor : "transparent"

                readonly property bool isSelected: navlistview.choiceIndex === index

                scale: 1.0
                Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutCubic } }


                // Hover Background (fades in/out)
                Rectangle {
                    anchors.fill: parent
                    radius: sidebar.styleSettings.labelRadius
                    color: sidebar.styleThemes.hoverColor
                    opacity: barMouse.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 80 } }
                }

                Text {
                    x: 8
                    y: 0
                    z: 1
                    width: 42
                    height: 42
                    text: model.iconChar
                    font.family: sidebar.iconFontFamily
                    font.pixelSize: sidebar.styleSettings.texticon
                    color: navDelegate.isSelected ? sidebar.choiceTextColor : sidebar.styleThemes.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Component.onCompleted: if (index === 2) visible = false
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                Text {
                    x: 56
                    y: 0
                    z: 2
                    width: 140
                    height: 42
                    text: model.display
                    color: navDelegate.isSelected ? sidebar.choiceTextColor : sidebar.styleThemes.textColor
                    font.bold: navDelegate.isSelected
                    font.pixelSize: sidebar.styleSettings.textmain
                    verticalAlignment: Text.AlignVCenter
                    Component.onCompleted: if (navDelegate.itemIndex === 2) visible = false
                    Behavior on color { ColorAnimation { duration: 120 } }
                }


                MouseArea {
                    id: barMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    Component.onCompleted: {
                        if(index == 2) visible = false
                    }
                    onPressed: navDelegate.scale = 0.96
                    onReleased: navDelegate.scale = 1.0
                    onCanceled: navDelegate.scale = 1.0
                    onClicked: {
                        sidebar.navigate(index);
                        forceActiveFocus();
                    }
                }
            }
        }
    }
}
