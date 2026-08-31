// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Controls.Basic
import QueMusic 1.0

QScrollView {
    id: root

    property var pluginManager: null
    property real containX: 0
    property real standWidth: width - 48
    property int selectedTab: 0
    readonly property var plugins: pluginManager ? pluginManager.plugins : []
    signal configureNavidromeRequested()

    clip: true

    Component {
        id: navidromeConfigButton
        Button {
            objectName: "navidromeConfigAction"
            text: "配置音源"
            onClicked: root.configureNavidromeRequested()
        }
    }

    contentChildren: Item {
        id: content
        width: root.availableWidth
        implicitHeight: contentColumn.implicitHeight + 24
        height: implicitHeight

        property real horizontalInset: Math.max(24, root.containX + 24)
        property real panelWidth: Math.max(0, Math.min(root.standWidth,
                                                        width - horizontalInset - 24))

        Column {
            id: contentColumn
            x: content.horizontalInset
            y: 24
            width: content.panelWidth
            spacing: 16

            Text {
                id: header
                objectName: "pluginPanelHeader"
                width: parent.width
                height: 36
                color: Style.themes.fontColor
                verticalAlignment: Text.AlignVCenter
                text: "插件"
                font.pixelSize: Style.settings.pageTitle
                font.weight: Font.DemiBold
                font.letterSpacing: -0.3
            }

            QBlurTapBar {
                id: tabs
                objectName: "pluginPanelTabs"
                width: 304
                height: 40
                model: ["外观类", "功能类", "音源"]
                tabWidth: 100
                rectXy: Qt.rect(0, 0, width, height)
                blurSource: root
                onTabChange: (index) => root.selectedTab = index
            }

            Rectangle {
                id: notice
                objectName: "pluginPanelNotice"
                width: parent.width
                height: noticeText.implicitHeight + 48
                color: Style.themes.containColor
                radius: Style.settings.cubeRadius
                border.color: Style.themes.sideColor
                border.width: 1

                Text {
                    x: 24
                    y: 24
                    font.family: typeof iconFont !== "undefined" && iconFont ? iconFont.name : ""
                    height: noticeText.implicitHeight
                    text: "\uf11a"
                    color: Style.themes.themeColor
                    font.pixelSize: Style.settings.texticon
                }
                Text {
                    id: noticeText
                    x: 48
                    y: 24
                    width: parent.width - 64
                    text: "原生插件可从应用或用户插件目录发现。状态“loaded”仅表示插件可用；音源插件仍需添加并启用账户后才能使用。"
                    wrapMode: Text.Wrap
                    color: Style.themes.textColor
                    font.pixelSize: Style.settings.textmain
                }
            }

            Item {
                width: parent.width
                implicitHeight: root.selectedTab === 2 ? musicContent.implicitHeight : 120
                height: implicitHeight

                Text {
                    anchors.fill: parent
                    visible: root.selectedTab !== 2
                    text: root.selectedTab === 0 ? "外观类" : "功能类"
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    color: Style.themes.textColor
                    font.pixelSize: Style.settings.textmain
                }

                Column {
                    id: musicContent
                    objectName: "pluginPanelList"
                    visible: root.selectedTab === 2
                    width: parent.width
                    spacing: 12

                    Row {
                        spacing: 10
                        Button {
                            text: "发现插件"
                            onClicked: {
                                if (root.pluginManager)
                                    root.pluginManager.discoverPlugins()
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "仅加载本地已安装且与当前 Qt 环境兼容的原生插件"
                            color: Style.themes.textColor
                            font.pixelSize: Style.settings.textmain
                        }
                    }

                    Text {
                        visible: !root.pluginManager || root.pluginManager.plugins.length === 0
                        width: parent.width
                        text: "未发现音源插件"
                        color: Style.themes.textColor
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Style.settings.textmain
                    }

                    Repeater {
                        model: root.plugins

                        delegate: Rectangle {
                            required property var modelData
                            objectName: "pluginCard_" + modelData.id
                            width: musicContent.width
                            height: Math.max(88, pluginDetails.implicitHeight + 24)
                            radius: Style.settings.cubeRadius
                            color: Style.themes.containColor
                            border.color: Style.themes.sideColor
                            border.width: 1

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 14

                                Column {
                                    id: pluginDetails
                                    width: Math.max(180, parent.width - pluginActions.implicitWidth - 14)
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4
                                    Text {
                                        width: parent.width
                                        text: modelData.name + " · " + modelData.version
                                        color: Style.themes.fontColor
                                        font.pixelSize: Style.settings.textmain
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: "状态：" + modelData.state
                                              + (modelData.activeLeases > 0
                                                 ? "（使用中：" + modelData.activeLeases + "）" : "")
                                              + (modelData.error ? " · " + modelData.error : "")
                                        color: Style.themes.textColor
                                        font.pixelSize: Style.settings.textTip
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: modelData.id === "navidrome" && modelData.state === "loaded"
                                        width: parent.width
                                        text: "插件已就绪；请配置并启用 Navidrome 账户后使用。"
                                        color: Style.themes.textColor
                                        font.pixelSize: Style.settings.textTip
                                        elide: Text.ElideRight
                                    }
                                }

                                Column {
                                    id: pluginActions
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    Loader {
                                        objectName: "navidromeConfigLoader"
                                        active: modelData.id === "navidrome" && modelData.state === "loaded"
                                        sourceComponent: navidromeConfigButton
                                    }
                                    Button {
                                        text: "加载"
                                        visible: modelData.loadable
                                        onClicked: root.pluginManager.loadPlugin(modelData.id)
                                    }
                                    Button {
                                        text: modelData.state === "failed" ? "重试卸载" : "卸载"
                                        visible: modelData.state === "loaded" || modelData.unloadable
                                        enabled: modelData.unloadable
                                        onClicked: root.pluginManager.unloadPlugin(modelData.id)
                                    }
                                    Button {
                                        text: "重载"
                                        enabled: modelData.reloadable
                                        onClicked: root.pluginManager.reloadPlugin(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
