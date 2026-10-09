// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Window {
    id: desktopSpot
    width: 320
    height: spotCard.height === 48 ? 48 : 120
    Component.onCompleted: x = Screen.width / 2 - 160
    y: 12
    visible: true
    color: "transparent"
    title: "DesktopSpot"
    transientParent: null
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    Rectangle {
        id: spotCard
        //x: 30
        y: 0
        //width: 180
        //height: 48
        radius: 24
        color: Style.themes.fontColor
        opacity: 0.8
        Component.onCompleted: {
            spotCard.state = "spotNormal"
        }
        states: [
            State {
                name: "spotNormal"
                PropertyChanges { target: spotCard; x: 40; height: 48; width: 240 }
                PropertyChanges { target: spotPlayButton; x: 200 }
                PropertyChanges { target: spotInfoPlayer; opacity: 0 }

            },
            State {
                name: "spotInfo"
                PropertyChanges { target: spotCard; x: 0; height: 120; width: 320 }
                PropertyChanges { target: spotPlayButton; x: 144 }
                PropertyChanges { target: spotInfoPlayer; opacity: 1 }
            }
        ]
        transitions: [

        Transition {
            to: "*"
            ParallelAnimation {
                NumberAnimation { target: spotCard; property: "x"; duration: 320; easing.type: Easing.Bezier; easing.bezierCurve: [ 0.23, 0.06, 0.00, 1.00, 1, 1 ] }
                NumberAnimation { target: spotCard; property: "width"; duration: 320; easing.type: Easing.Bezier; easing.bezierCurve: [ 0.23, 0.06, 0.00, 1.00, 1, 1 ] }
                NumberAnimation { target: spotCard; property: "height"; duration: 320; easing.type: Easing.Bezier; easing.bezierCurve: [ 0.23, 0.06, 0.00, 1.00, 1, 1 ] }
                NumberAnimation { target: spotPlayButton; property: "x"; duration: 320; easing.type: Easing.Bezier; easing.bezierCurve: [ 0.23, 0.06, 0.00, 1.00, 1, 1 ] }
                NumberAnimation { target: spotInfoPlayer; property: "opacity"; duration: 320; easing.type: Easing.OutExpo }
            }
        }
        ]

        Rectangle {
            y: 8
            x: 8
            z: 4
            width: 32
            height: 32
            color: Style.themes.primaryColor
            radius: 16
            Text {
                anchors.fill: parent
                text: "\uf044"
                font.family: iconFont.name
                font.pixelSize: Style.settings.texticon
                color: Style.themes.fontColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            id: spotTitle
            objectName: "desktopSpotTitle"
            x: 48
            y: 8
            width: spotCard.width - 96
            height: 32
            text: window.lyricsAdapter.details.title || ""
            textFormat: Text.PlainText
            color: "white"
            font.pixelSize: 13
            clip: true
            verticalAlignment: Text.AlignVCenter
        }
        // 主控制按钮，常驻
        SButton {
            id: spotPlayButton
            objectName: "desktopSpotPlayButton"
            //x: 104
            y: spotCard.height - 40
            z: 3
            iconCharacter: window.lyricsAdapter.playing ? "\uf02f" : "\uf00e"
            enabled: window.lyricsAdapter.active
            width: 32
            height: 32
            radius: 16
            buttonColor: Style.themes.secondaryBlurColor
            hoverColor: Style.themes.primaryColor
            iconColor: Style.themes.textColor
            iconSize: Style.settings.texticon
            shadowEnabled: false
            onClicked: window.lyricsAdapter.togglePlayback()
        }

        // 控制
        Item {
            visible: opacity !== 0
            id: spotInfoPlayer
            y: spotCard.height - 64
            height: 32
            width: 104
            anchors.horizontalCenter: parent.horizontalCenter
            z: 2
            // 进度条
            Slider {
                id: seekSlider
                objectName: "desktopSpotSeekSlider"
                y: 0
                width: parent.width
                height: 16
                from: 0
                to: window.lyricsAdapter.duration > 0 ? window.lyricsAdapter.duration : 1
                enabled: window.lyricsAdapter.seekable
                Binding {
                    target: seekSlider
                    property: "value"
                    // Re-evaluate after a duration/mode switch: Slider can clamp
                    // a new position against the previous occurrence's range.
                    value: Math.min(seekSlider.to, Math.max(0, window.lyricsAdapter.position))
                    when: !seekSlider.pressed
                    restoreMode: Binding.RestoreNone
                }
                live: true
                onMoved: window.lyricsAdapter.seek(value)
                padding: 0
                background: Rectangle {
                    y: (seekSlider.height - 4) / 2
                    width: seekSlider.availableWidth
                    height: 4
                    radius: 2
                    color: Style.themes.secondaryColor
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: Style.themes.themeColor
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: (seekSlider.height - 12) / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: Style.themes.primaryColor
                    border.width: 2
                    border.color: Style.themes.themeColor
                    visible: seekSlider.hovered || seekSlider.pressed
                }
            }
            SButton {
                objectName: "desktopSpotPreviousButton"
                enabled: window.lyricsAdapter.active
                x: 0
                y: 24
                iconCharacter: "\uf0dc"
                width: 32
                height: 32
                radius: 16
                buttonColor: "transparent"
                hoverColor: Style.themes.secondaryBlurColor
                iconColor: Style.themes.primaryColor
                iconSize: Style.settings.texticon
                shadowEnabled: false
                onClicked: if (window.lyricsAdapter.active) musicControlMin.lastMedia()
            }

            SButton {
                objectName: "desktopSpotNextButton"
                enabled: window.lyricsAdapter.active
                x: 72
                y: 24
                iconCharacter: "\uf0d9"
                width: 32
                height: 32
                radius: 16
                buttonColor: "transparent"
                hoverColor: Style.themes.secondaryBlurColor
                iconColor: Style.themes.primaryColor
                iconSize: Style.settings.texticon
                shadowEnabled: false
                onClicked: if (window.lyricsAdapter.active) musicControlMin.enterMedia()
            }
        }



        MouseArea {
            anchors.fill: parent
            z: 1
            onClicked: {
                if(spotCard.state === "spotInfo") {
                    spotCard.state = "spotNormal"
                } else {
                    spotCard.state = "spotInfo"
                }
            }
        }
    }
}
