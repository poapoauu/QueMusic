import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

CheckBox {
    id: root
    activeFocusOnTab: true
    Accessible.name: text
    font: Qt.font({ family: PluginTheme.fontBody.family,
                    pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
    implicitHeight: 36 * PluginTheme.scaleFactor
    indicator: Rectangle {
        implicitWidth: 20 * PluginTheme.scaleFactor
        implicitHeight: implicitWidth
        x: root.leftPadding
        y: (root.height - height) / 2
        radius: PluginTheme.radiusSmall / 2
        color: root.checked ? PluginTheme.primary : PluginTheme.surface
        border.color: root.activeFocus ? PluginTheme.primary : PluginTheme.border
        border.width: root.activeFocus ? 2 : 1
        Text {
            anchors.centerIn: parent
            text: root.checked ? "✓" : ""
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
