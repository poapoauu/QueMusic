import QtQuick
import QueMusic.PluginUI 1.0

Item {
    id: root
    property url source: ""
    readonly property bool validSource: {
        const value = source.toString()
        const packageRelative = value.length > 0
            && !/^[A-Za-z][A-Za-z0-9+.-]*:/.test(value)
            && !value.startsWith("/")
            && value.split("/").indexOf("..") === -1
        return packageRelative || value.startsWith("qrc:/")
            || value.startsWith("file:/") || value.startsWith("image://")
    }
    readonly property bool error: !validSource || qrImage.status === Image.Error
    implicitWidth: 180 * PluginTheme.scaleFactor
    implicitHeight: implicitWidth
    Accessible.name: error ? qsTr("QR code unavailable") : qsTr("QR code")

    Image {
        id: qrImage
        anchors.fill: parent
        source: root.validSource ? root.source : ""
        fillMode: Image.PreserveAspectFit
        visible: !root.error
    }
    PluginErrorState {
        anchors.centerIn: parent
        title: qsTr("QR code unavailable")
        visible: root.error
    }
}
