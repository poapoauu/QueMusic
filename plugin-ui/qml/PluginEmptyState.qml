import QtQuick
import QueMusic.PluginUI 1.0

Column {
    id: root
    property string title: ""
    property string description: ""
    property string actionText: ""
    property bool actionVisible: false
    signal actionRequested()
    spacing: PluginTheme.spacingSmall
    width: 240 * PluginTheme.scaleFactor
    PluginLabel { width: root.width; text: root.title; visible: text.length > 0 }
    PluginDescription { width: root.width; text: root.description; visible: text.length > 0 }
    PluginButton {
        objectName: "pluginEmptyAction"
        width: Math.min(implicitWidth, root.width)
        text: root.actionText
        visible: root.actionVisible
        onClicked: root.actionRequested()
    }
}
