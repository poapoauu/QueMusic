import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

ComboBox {
    id: root
    activeFocusOnTab: true
    Accessible.name: displayText
    font: Qt.font({ family: PluginTheme.fontBody.family,
                    pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
    implicitWidth: 220 * PluginTheme.scaleFactor
    implicitHeight: 36 * PluginTheme.scaleFactor
    background: Rectangle {
        color: PluginTheme.surface
        radius: PluginTheme.radiusSmall
        border.color: root.activeFocus ? PluginTheme.primary : PluginTheme.border
        border.width: root.activeFocus ? 2 : 1
    }
    contentItem: Text {
        text: root.displayText
        color: PluginTheme.textPrimary
        font: Qt.font({ family: PluginTheme.fontBody.family,
                        pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
        verticalAlignment: Text.AlignVCenter
        leftPadding: PluginTheme.spacingMedium
    }
}
