import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

BusyIndicator {
    id: root
    readonly property bool animating: running && !PluginTheme.reducedMotion
    implicitWidth: 28 * PluginTheme.scaleFactor
    implicitHeight: implicitWidth
    Accessible.name: qsTr("Loading")
    contentItem: Item {
        Rectangle {
            id: ring
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) - 4
            height: width
            radius: width / 2
            color: "transparent"
            border.color: PluginTheme.primary
            border.width: 3
            opacity: root.running ? 1 : 0.4
            RotationAnimation on rotation {
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
                running: root.animating
            }
        }
    }
}
