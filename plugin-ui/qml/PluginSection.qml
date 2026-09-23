import QtQuick
import QueMusic.PluginUI 1.0

Column {
    id: root
    property string title: ""
    spacing: PluginTheme.spacingMedium
    width: parent ? parent.width : implicitWidth
    PluginLabel { text: root.title; visible: root.title.length > 0 }
}
