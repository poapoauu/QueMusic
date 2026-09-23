import QtQuick
import QueMusic.PluginUI 1.0

Text {
    color: PluginTheme.textPrimary
    font: Qt.font({ family: PluginTheme.fontBody.family,
                    pixelSize: PluginTheme.fontBody.pixelSize * PluginTheme.scaleFactor })
    wrapMode: Text.Wrap
    Accessible.name: text
}
