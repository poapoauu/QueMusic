// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors

import QtQuick
import QtQuick.Controls.Basic
import QueMusic 1.0
import "PluginSettingsText.js" as PluginText

Item {
    id: root

    property var instances: []
    property string selectedInstanceId: ""
    property bool busy: false

    signal selectInstanceRequested(string instanceId)
    signal newInstanceRequested()
    signal removeInstanceRequested(string instanceId)
    signal enabledRequested(string instanceId, bool enabled)

    implicitHeight: heading.height + 8 + accountList.height

    Row {
        id: heading
        width: parent.width
        height: 36
        spacing: 10

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - addButton.width - parent.spacing)
            text: qsTr("Accounts")
            color: Style.themes.fontColor
            font.pixelSize: Style.settings.textmain
            font.weight: Font.DemiBold
        }

        Button {
            id: addButton
            objectName: "newInstanceAction"
            text: qsTr("New account")
            enabled: !root.busy
            palette.button: Style.themes.containColor
            palette.buttonText: Style.themes.fontColor
            onClicked: root.newInstanceRequested()
        }
    }

    ListView {
        id: accountList
        objectName: "sourceAccountList"
        anchors.top: heading.bottom
        anchors.topMargin: 8
        width: parent.width
        height: Math.min(contentHeight, 190)
        implicitHeight: height
        clip: true
        spacing: 8
        model: root.instances || []

        delegate: Rectangle {
            required property var modelData
            width: accountList.width
            height: Math.max(64, accountContent.implicitHeight + 16)
            radius: Style.settings.cubeRadius
            color: modelData.sourceInstanceId === root.selectedInstanceId
                   ? Style.themes.sideColor : Style.themes.containColor
            border.color: Style.themes.sideColor
            border.width: 1

            Row {
                id: accountContent
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Button {
                    objectName: "instanceSelect_" + modelData.sourceInstanceId
                    width: Math.max(110, parent.width - enableButton.width - removeButton.width
                                    - parent.spacing * 2)
                    height: Math.max(44, accountLabels.implicitHeight + 8)
                    palette.button: "transparent"
                    palette.buttonText: Style.themes.fontColor
                    background: Rectangle { color: "transparent" }
                    contentItem: Column {
                        id: accountLabels
                        spacing: 2
                        Text {
                            width: parent.width
                            text: modelData.displayName || qsTr("Unnamed account")
                            color: Style.themes.fontColor
                            wrapMode: Text.Wrap
                            font.pixelSize: Style.settings.textmain
                        }
                        Text {
                            width: parent.width
                            text: PluginText.sessionState(modelData.state)
                            color: Style.themes.textColor
                            wrapMode: Text.Wrap
                            font.pixelSize: Style.settings.textTip
                        }
                    }
                    onClicked: root.selectInstanceRequested(modelData.sourceInstanceId)
                }

                Button {
                    id: enableButton
                    objectName: "instanceEnable_" + modelData.sourceInstanceId
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.enabled ? qsTr("Disable") : qsTr("Enable")
                    enabled: !root.busy
                    palette.button: Style.themes.containColor
                    palette.buttonText: Style.themes.fontColor
                    onClicked: root.enabledRequested(modelData.sourceInstanceId, !modelData.enabled)
                }

                Button {
                    id: removeButton
                    objectName: "instanceRemove_" + modelData.sourceInstanceId
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Remove")
                    enabled: !root.busy
                    palette.button: Style.themes.containColor
                    palette.buttonText: Style.themes.fontColor
                    onClicked: root.removeInstanceRequested(modelData.sourceInstanceId)
                }
            }
        }
    }
}
