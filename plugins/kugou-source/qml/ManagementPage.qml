import QtQuick
import QueMusic.PluginUI 1.0

PluginScrollPage {
    id: root
    objectName: "kugouManagementPage"
    property var pluginUiContext: null
    readonly property var backend: pluginUiContext && pluginUiContext.valid ? pluginUiContext.backend : null
    readonly property string authState: backend ? backend.state : "invalidated"
    readonly property bool saving: authState === "storing" || authState === "clearing"
    readonly property bool active: authState === "fetching" || authState === "waiting" || authState === "scanned"
    title: qsTr("Kugou account (experimental)")
    implicitHeight: contentColumn.implicitHeight + 2 * PluginTheme.spacingLarge
    Column {
        id: contentColumn
        width: Math.max(0, parent.width - 2 * PluginTheme.spacingLarge)
        spacing: PluginTheme.spacingMedium
        PluginLabel {
            width: parent.width
            text: root.title
            font: PluginTheme.fontTitle
        }
        PluginDescription {
            width: parent.width
            text: qsTr("Experimental credential binding only. Stored credentials are not verified login or permission to access account features.")
        }
        PluginAccountCard {
            objectName: "kugouAccountCard"
            width: parent.width
            title: root.backend && root.backend.credentialStored ? qsTr("Candidate credentials stored") : qsTr("No candidate credentials stored")
            subtitle: qsTr("Clear the existing binding before switching accounts. No legacy Host account is imported.")
        }
        PluginQrLogin {
            objectName: "kugouQrLogin"
            qrPayload: root.backend ? root.backend.qrUrl.toString() : ""
            status: root.authState === "failed" || root.authState === "expired" ? "error" : "neutral"
            statusText: root.authState === "stored" ? qsTr("Stored; service verification pending") : root.authState
            busy: root.saving || root.authState === "fetching"
            refreshEnabled: root.backend !== null && !root.saving
            cancelEnabled: root.backend !== null && root.active && !root.saving
            onRefreshRequested: { if (root.backend && !root.saving) root.backend.startQr() }
            onCancelRequested: { if (root.backend && !root.saving) root.backend.cancelQr() }
        }
        PluginStatus {
            width: parent.width
            status: "error"
            text: root.backend ? root.backend.errorKey : ""
            visible: text.length > 0
        }
        PluginDangerButton {
            objectName: "kugouClearAccount"
            text: qsTr("Clear candidate credentials")
            enabled: root.backend !== null && !root.saving
            onClicked: { if (root.backend && !root.saving) root.backend.clearAccount() }
        }
    }
}
