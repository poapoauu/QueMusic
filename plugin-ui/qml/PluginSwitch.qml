import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

Switch {
    id: root
    activeFocusOnTab: true
    Accessible.name: text
    font: Qt.font({ family: PluginTheme.fontBody.family,
                    pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
    implicitHeight: 36 * PluginTheme.scaleFactor
    indicator: Rectangle {
        implicitWidth: 42 * PluginTheme.scaleFactor
        implicitHeight: 24 * PluginTheme.scaleFactor
        x: root.leftPadding
        y: (root.height - height) / 2
        radius: height / 2
        color: root.checked ? PluginTheme.primary : PluginTheme.surface
        border.color: root.activeFocus ? PluginTheme.primary : PluginTheme.border
        border.width: root.activeFocus ? 2 : 1
        Rectangle {
            width: parent.height - 6
            height: width
            radius: width / 2
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: PluginTheme.background
        }
    }
    contentItem: Text {
        text: root.text
        color: root.enabled ? PluginTheme.textPrimary : PluginTheme.textSecondary
        font: Qt.font({ family: PluginTheme.fontBody.family,
                        pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + PluginTheme.spacingSmall
    }
}
