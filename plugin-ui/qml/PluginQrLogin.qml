import QtQuick
import QueMusic.PluginUI 1.0

Column {
    id: root
    property url qrSource: ""
    property string qrPayload: ""
    property bool refreshEnabled: true
    property bool cancelEnabled: true
    property string status: "neutral"
    property string statusText: ""
    property bool busy: false
    signal refreshRequested()
    signal cancelRequested()
    spacing: PluginTheme.spacingMedium

    PluginQrCode { source: root.qrSource; payload: root.qrPayload }
    PluginStatus { status: root.status; text: root.statusText; visible: text.length > 0 }
    PluginBusyIndicator { running: root.busy; visible: root.busy }
    Row {
        spacing: PluginTheme.spacingSmall
        PluginButton {
            objectName: "pluginQrRefresh"
            text: qsTr("Refresh")
            enabled: root.refreshEnabled
            onClicked: root.refreshRequested()
        }
        PluginButton {
            objectName: "pluginQrCancel"
            text: qsTr("Cancel")
            enabled: root.cancelEnabled
            onClicked: root.cancelRequested()
        }
    }
}
