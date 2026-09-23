import QtQuick
import QueMusic.PluginUI 1.0

Row {
    id: root
    property string status: "neutral"
    property string text: ""
    readonly property color statusColor:
        status === "success" ? PluginTheme.success
      : status === "warning" ? PluginTheme.warning
      : status === "error" ? PluginTheme.danger
      : PluginTheme.textSecondary
    spacing: PluginTheme.spacingSmall
    Accessible.name: text

    Rectangle {
        width: 8 * PluginTheme.scaleFactor
        height: width
        radius: width / 2
        color: root.statusColor
        anchors.verticalCenter: parent.verticalCenter
    }
    PluginLabel {
        text: root.text
        color: root.statusColor
    }
}
