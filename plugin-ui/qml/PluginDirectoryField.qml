import QtQuick
import QueMusic.PluginUI 1.0

Item {
    id: root
    property alias text: pathField.text
    property string placeholderText: ""
    signal browseRequested()
    implicitWidth: 300 * PluginTheme.scaleFactor
    implicitHeight: Math.max(pathField.implicitHeight, browseButton.implicitHeight)

    PluginTextField {
        id: pathField
        readOnly: true
        placeholderText: root.placeholderText
        width: root.width - browseButton.width - PluginTheme.spacingSmall
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
    }
    PluginButton {
        id: browseButton
        objectName: "pluginDirectoryBrowseButton"
        text: "Browse"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.browseRequested()
    }
}
