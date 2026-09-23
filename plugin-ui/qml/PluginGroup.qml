import QtQuick
import QueMusic.PluginUI 1.0

Rectangle {
    id: root
    default property alias content: body.data
    color: PluginTheme.surface
    radius: PluginTheme.radiusMedium
    border.color: PluginTheme.border
    implicitWidth: body.implicitWidth + 2 * PluginTheme.spacingMedium
    implicitHeight: body.implicitHeight + 2 * PluginTheme.spacingMedium
    Column {
        id: body
        anchors.fill: parent
        anchors.margins: PluginTheme.spacingMedium
        spacing: PluginTheme.spacingSmall
    }
}
