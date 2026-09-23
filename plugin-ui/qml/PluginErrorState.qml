import QtQuick
import QueMusic.PluginUI 1.0

Column {
    id: root
    property string title: ""
    property string description: ""
    property string actionText: qsTr("Retry")
    property bool actionVisible: false
    signal retryRequested()
    spacing: PluginTheme.spacingSmall
    PluginLabel { text: root.title; color: PluginTheme.danger; visible: text.length > 0 }
    PluginDescription { text: root.description; visible: text.length > 0 }
    PluginButton {
        objectName: "pluginErrorAction"
        text: root.actionText
        visible: root.actionVisible
        onClicked: root.retryRequested()
    }
}
