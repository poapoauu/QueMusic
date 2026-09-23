import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

Button {
    id: root
    property color accentColor: PluginTheme.surface
    Accessible.name: text
    activeFocusOnTab: true
    implicitHeight: 36 * PluginTheme.scaleFactor
    implicitWidth: Math.max(80 * PluginTheme.scaleFactor,
                            label.implicitWidth + 2 * PluginTheme.spacingMedium)
    leftPadding: PluginTheme.spacingMedium
    rightPadding: PluginTheme.spacingMedium
    background: Rectangle {
        radius: PluginTheme.radiusMedium
        color: !root.enabled ? PluginTheme.surface
              : root.down ? Qt.darker(root.accentColor, 1.08)
              : root.hovered ? PluginTheme.surfaceHover : root.accentColor
        border.color: root.activeFocus ? PluginTheme.primary : PluginTheme.border
        border.width: root.activeFocus ? 2 : 1
    }
    contentItem: Text {
        id: label
        text: root.text
        color: root.enabled ? PluginTheme.textPrimary : PluginTheme.textSecondary
        font: Qt.font({ family: PluginTheme.fontBody.family,
                        pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
