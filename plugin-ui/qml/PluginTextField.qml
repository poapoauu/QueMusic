import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

TextField {
    id: root
    activeFocusOnTab: true
    Accessible.name: placeholderText.length > 0 ? placeholderText : text
    color: enabled ? PluginTheme.textPrimary : PluginTheme.textSecondary
    font: Qt.font({ family: PluginTheme.fontBody.family,
                    pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
    implicitHeight: 36 * PluginTheme.scaleFactor
    implicitWidth: 220 * PluginTheme.scaleFactor
    leftPadding: PluginTheme.spacingMedium
    rightPadding: PluginTheme.spacingMedium
    background: Rectangle {
        color: PluginTheme.surface
        radius: PluginTheme.radiusSmall
        border.color: root.activeFocus ? PluginTheme.primary : PluginTheme.border
        border.width: root.activeFocus ? 2 : 1
    }
}
