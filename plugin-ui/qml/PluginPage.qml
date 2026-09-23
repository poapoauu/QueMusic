import QtQuick
import QtQuick.Layouts
import QueMusic.PluginUI 1.0

Rectangle {
    id: root
    property string title: ""
    default property alias content: body.data
    color: PluginTheme.background
    implicitWidth: 480
    implicitHeight: layout.implicitHeight + 2 * PluginTheme.spacingLarge
    Accessible.name: title

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: PluginTheme.spacingLarge
        spacing: PluginTheme.spacingMedium

        Text {
            text: root.title
            color: PluginTheme.textPrimary
            font: Qt.font({ family: PluginTheme.fontTitle.family,
                            pixelSize: PluginTheme.fontTitle.pixelSize * PluginTheme.scaleFactor })
            visible: text.length > 0
            Layout.fillWidth: true
        }
        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
