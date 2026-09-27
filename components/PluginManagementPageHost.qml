import QtQuick
import QtQuick.Controls.Basic
import QueMusic.PluginUI 1.0

Item {
    id: root
    objectName: "managementPageHost"
    property var controller: null
    property bool closing: false
    property bool loadFailed: false
    readonly property var session: controller ? controller.managementUiSession : null

    function loadPage() {
        if (!session || session.state !== "ready" || !session.context || !session.context.valid) return
        loadFailed = false
        managementLoader.active = true
        managementLoader.setSource(session.componentUrl,
                                   { "pluginUiContext": session.context })
    }
    function closePage() {
        if (closing || !controller || !session) return
        closing = true
        controller.beginCloseManagementUi()
        managementLoader.active = false
        managementLoader.source = ""
        controller.finishCloseManagementUi()
        loadFailed = false
        closing = false
    }
    Component.onCompleted: loadPage()
    Component.onDestruction: closePage()
    onVisibleChanged: { if (!visible) closePage() }
    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onManagementUiChanged() {
            if (root.session && managementLoader.source.toString().length === 0)
                root.loadPage()
        }
        function onManagementUiCloseRequested() { root.closePage() }
    }

    Rectangle {
        anchors.fill: parent
        color: PluginTheme.background
        border.color: PluginTheme.border
        radius: PluginTheme.radiusMedium
    }
    Column {
        anchors.fill: parent
        anchors.margins: PluginTheme.spacingLarge
        spacing: PluginTheme.spacingMedium
        Row {
            width: parent.width
            spacing: PluginTheme.spacingMedium
            PluginButton {
                objectName: "closeManagementUiAction"
                text: qsTr("Back to accounts")
                onClicked: root.closePage()
            }
            PluginLabel {
                text: qsTr("Plugin management")
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Item {
            width: parent.width
            height: parent.height - y
            Loader {
                id: managementLoader
                objectName: "managementPageLoader"
                anchors.fill: parent
                asynchronous: true
                onLoaded: {
                    if (root.session && item) root.session.trackPage(item)
                }
                onStatusChanged: {
                    if (status === Loader.Error) root.loadFailed = true
                }
            }
            PluginBusyIndicator {
                anchors.centerIn: parent
                running: managementLoader.status === Loader.Loading
                visible: running
            }
            PluginErrorState {
                objectName: "managementPageError"
                anchors.centerIn: parent
                visible: root.loadFailed || (!!root.session && root.session.state === "error")
                title: qsTr("Unable to open plugin page")
                description: qsTr("The plugin management page is unavailable.")
                actionVisible: true
                onRetryRequested: root.loadPage()
            }
        }
    }
}
