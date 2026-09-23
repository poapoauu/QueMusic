import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

ScrollView {
    id: root
    property string title: ""
    default property alias content: column.data
    clip: true
    implicitWidth: 480
    background: Rectangle { color: PluginTheme.background }
    contentWidth: availableWidth

    Column {
        id: column
        width: root.availableWidth
        spacing: PluginTheme.spacingMedium
        padding: PluginTheme.spacingLarge
    }
}
