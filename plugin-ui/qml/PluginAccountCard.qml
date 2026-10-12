import QtQuick
import QueMusic.PluginUI 1.0

PluginGroup {
    id: root
    property string title: ""
    property string subtitle: ""
    property string status: "neutral"
    property string statusText: ""
    property string actionText: ""
    signal actionRequested()
    implicitWidth: 280 * PluginTheme.scaleFactor

    PluginLabel { width: parent.width; text: root.title; visible: text.length > 0 }
    PluginDescription { width: parent.width; text: root.subtitle; visible: text.length > 0 }
    PluginStatus { width: parent.width; status: root.status; text: root.statusText; visible: text.length > 0 }
    PluginButton {
        objectName: "pluginAccountAction"
        text: root.actionText
        visible: text.length > 0
        onClicked: root.actionRequested()
    }
}
