import QtQuick
import QueMusic.PluginUI 1.0

Text {
    color: PluginTheme.textSecondary
    font: Qt.font({ family: PluginTheme.fontCaption.family,
                    pixelSize: PluginTheme.fontCaption.pixelSize * PluginTheme.scaleFactor })
    wrapMode: Text.Wrap
    Accessible.name: text
}
