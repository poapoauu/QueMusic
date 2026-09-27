import QtQuick
import QueMusic.PluginUI 1.0

PluginPage {
    id: root
    objectName: "fixtureManagementPage"
    property var pluginUiContext: null
    title: "Offline QR fixture"
    Column {
        width: parent.width
        spacing: PluginTheme.spacingMedium
        PluginQrLogin {
            objectName: "fixtureQrLogin"
            status: root.pluginUiContext && root.pluginUiContext.backend
                    && root.pluginUiContext.backend.loginState === "success" ? "success" : "neutral"
            statusText: root.pluginUiContext && root.pluginUiContext.backend
                        ? root.pluginUiContext.backend.loginState : "idle"
            busy: statusText === "pending"
            onRefreshRequested: {
                if (root.pluginUiContext && root.pluginUiContext.valid)
                    root.pluginUiContext.backend.requestLogin()
            }
            onCancelRequested: {
                if (root.pluginUiContext && root.pluginUiContext.valid)
                    root.pluginUiContext.backend.cancelLogin()
            }
        }
    PluginSettingsForm {
        objectName: "fixtureSettingsForm"
            width: parent.width
            settings: root.pluginUiContext ? root.pluginUiContext.settings : null
            host: root.pluginUiContext ? root.pluginUiContext.host : null
        }
    }
}
