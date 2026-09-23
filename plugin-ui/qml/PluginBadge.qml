import QtQuick
import QueMusic.PluginUI 1.0

Rectangle {
    id: root
    property string status: "neutral"
    property string text: ""
    readonly property color statusColor:
        status === "success" ? PluginTheme.success
      : status === "warning" ? PluginTheme.warning
      : status === "error" ? PluginTheme.danger
      : PluginTheme.textSecondary
    color: PluginTheme.surface
    border.color: root.statusColor
    radius: PluginTheme.radiusLarge
    implicitWidth: text.length > 0 ? label.implicitWidth + 2 * PluginTheme.spacingSmall : 0
    implicitHeight: text.length > 0 ? label.implicitHeight + PluginTheme.spacingSmall : 0
    Accessible.name: text
    PluginDescription {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.statusColor
    }
}
