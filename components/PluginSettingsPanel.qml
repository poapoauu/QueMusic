// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QueMusic 1.0
import "PluginSettingsText.js" as PluginText

Rectangle {
    id: root

    property var controller: null
    property real containX: 0
    property real standWidth: width - 48
    readonly property bool compact: width < 760
    readonly property var plugins: controller ? controller.plugins : []
    readonly property var selectedPlugin: controller ? controller.selectedPlugin : ({})
    readonly property bool busy: controller ? controller.busy : false
    readonly property color surfaceColor: Style.themes.containColor
    readonly property color primaryTextColor: Style.themes.fontColor
    readonly property color controlColor: Style.themes.primaryColor
    readonly property bool darkTheme: Style.darkis

    property var formSections: []
    property string formContextKey: ""
    property string pendingRemovalId: ""
    property string pendingActionId: ""
    property string directoryFieldId: ""
    property string observedPluginState: ""
    property string pendingProbeRequest: ""
    property bool probeCurrent: true

    color: Style.themes.backgroundColor || Style.themes.containColor
    clip: true

    function currentInstance() {
        if (!controller)
            return null
        var source = controller.instances || []
        for (var index = 0; index < source.length; ++index) {
            if (source[index].sourceInstanceId === controller.selectedInstanceId)
                return source[index]
        }
        return null
    }

    function sanitizedSections(rawSections) {
        var result = []
        var source = rawSections || []
        for (var sectionIndex = 0; sectionIndex < source.length; ++sectionIndex) {
            var sectionSource = source[sectionIndex]
            var sectionCopy = {}
            for (var sectionKey in sectionSource) {
                if (sectionKey !== "fields")
                    sectionCopy[sectionKey] = sectionSource[sectionKey]
            }
            var safeFields = []
            var fields = sectionSource.fields || []
            for (var fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
                var fieldSource = fields[fieldIndex]
                var fieldCopy = {}
                var secret = fieldSource.type === 1 || fieldSource.secret === true
                for (var fieldKey in fieldSource) {
                    if (!secret || fieldKey !== "value")
                        fieldCopy[fieldKey] = fieldSource[fieldKey]
                }
                safeFields.push(fieldCopy)
            }
            sectionCopy.fields = safeFields
            result.push(sectionCopy)
        }
        return result
    }

    function refreshFromController(forceReset) {
        if (!controller) {
            formContextKey = ""
            formSections = []
            displayNameInput.text = ""
            schemaForm.resetFromSections([])
            observedPluginState = ""
            pendingProbeRequest = ""
            probeCurrent = false
            return
        }
        var nextContext = String(controller.selectedPluginId || "") + "\n"
                          + String(controller.selectedInstanceId || "")
        if (forceReset || nextContext !== formContextKey) {
            formContextKey = nextContext
            formSections = sanitizedSections(controller.settingsSections)
            schemaForm.resetFromSections(formSections)
            var account = currentInstance()
            displayNameInput.text = account ? String(account.displayName || "") : ""
            pendingProbeRequest = ""
            probeCurrent = true
        } else {
            formSections = sanitizedSections(controller.settingsSections)
            schemaForm.mergeFromSections(formSections)
        }
        var nextPluginState = String(selectedPlugin.state || "")
        if (nextPluginState === "unloaded" && observedPluginState !== "unloaded")
            schemaForm.clearSecrets()
        observedPluginState = nextPluginState
    }

    function selectPlugin(packageId) {
        if (!controller || busy)
            return
        schemaForm.clearSecrets()
        controller.selectPlugin(packageId)
        refreshFromController(false)
    }

    function selectInstance(instanceId) {
        if (!controller || busy)
            return
        schemaForm.clearSecrets()
        controller.selectInstance(instanceId)
        refreshFromController(false)
    }

    function saveInstance() {
        if (!controller || !schemaForm.synchronizePublicDraft())
            return
        var accepted = controller.saveInstance(displayNameInput.text,
                                               schemaForm.collectSecretDraft())
        if (accepted) {
            schemaForm.clearSecrets()
            formSections = sanitizedSections(controller.settingsSections)
            schemaForm.resetFromSections(formSections)
            probeCurrent = false
        }
    }

    function testConnection() {
        if (!controller || !schemaForm.synchronizePublicDraft())
            return
        pendingProbeRequest = String(controller.testConnection(schemaForm.collectSecretDraft()))
    }

    function runAction(actionId, confirmed) {
        if (!controller || !probeCurrent || !schemaForm.synchronizePublicDraft())
            return
        controller.runSettingsAction(actionId, schemaForm.collectSecretDraft(), confirmed)
    }

    function requestAction(actionData) {
        if (actionData.requiresConfirmation) {
            pendingActionId = actionData.id
            actionConfirmation.open()
        } else {
            runAction(actionData.id, false)
        }
    }

    function cancelDraft() {
        if (controller)
            controller.cancelOperation()
        schemaForm.clearSecrets()
        formSections = controller ? sanitizedSections(controller.settingsSections) : []
        schemaForm.resetFromSections(formSections)
    }

    function filteredActions() {
        var result = []
        var source = controller ? controller.settingsActions || [] : []
        for (var index = 0; index < source.length; ++index) {
            if (source[index].state !== 0)
                result.push(source[index])
        }
        return result
    }

    onControllerChanged: {
        schemaForm.clearSecrets()
        refreshFromController(true)
    }
    onVisibleChanged: {
        if (!visible)
            schemaForm.clearSecrets()
    }
    Component.onCompleted: refreshFromController(true)
    Component.onDestruction: schemaForm.clearSecrets()

    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onSnapshotsChanged() { root.refreshFromController(false) }
        function onDraftReset() {
            schemaForm.clearSecrets()
            root.formSections = root.controller
                    ? root.sanitizedSections(root.controller.settingsSections) : []
            schemaForm.resetFromSections(root.formSections)
            root.pendingProbeRequest = ""
            root.probeCurrent = true
        }
        function onConnectionTestFinished(requestId, result) {
            if (!root.pendingProbeRequest
                || String(requestId) !== root.pendingProbeRequest)
                return
            root.pendingProbeRequest = ""
            root.probeCurrent = result.success === true
        }
    }

    Dialog {
        id: removalConfirmation
        objectName: "instanceRemovalConfirmation"
        parent: root
        modal: true
        title: qsTr("Remove account?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.window: Style.themes.containColor
        palette.windowText: Style.themes.fontColor
        onAccepted: {
            if (root.controller && root.pendingRemovalId)
                root.controller.removeInstance(root.pendingRemovalId)
            root.pendingRemovalId = ""
        }
        onRejected: root.pendingRemovalId = ""
    }

    Dialog {
        id: actionConfirmation
        objectName: "settingsActionConfirmation"
        parent: root
        modal: true
        title: qsTr("Run this plugin action?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.window: Style.themes.containColor
        palette.windowText: Style.themes.fontColor
        onAccepted: {
            var actionId = root.pendingActionId
            root.pendingActionId = ""
            if (actionId)
                root.runAction(actionId, true)
        }
        onRejected: root.pendingActionId = ""
    }

    FolderDialog {
        id: directoryDialog
        objectName: "pluginDirectoryDialog"
        title: qsTr("Choose a local folder")
        onAccepted: {
            if (!root.controller || !root.directoryFieldId)
                return
            var localFolder = selectedFolder
            schemaForm.acceptControllerValue(root.directoryFieldId)
            if (root.controller.setDirectoryField(root.directoryFieldId, localFolder)) {
                root.formSections = root.sanitizedSections(root.controller.settingsSections)
                schemaForm.mergeFromSections(root.formSections)
                root.probeCurrent = false
            }
            root.directoryFieldId = ""
        }
        onRejected: root.directoryFieldId = ""
    }

    Item {
        id: page
        x: Math.max(12, Math.min(root.width - 12, root.containX + 12))
        y: 12
        width: Math.max(0, Math.min(root.standWidth, root.width - x - 12))
        height: Math.max(0, root.height - 24)

        Item {
            id: header
            objectName: "pluginPanelHeader"
            width: parent.width
            height: headerContent.implicitHeight

            Column {
                id: headerContent
                width: parent.width
                spacing: 8

                Text {
                    width: parent.width
                    text: qsTr("Plugins")
                    color: Style.themes.fontColor
                    font.pixelSize: Style.settings.pageTitle
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }

                Flow {
                    width: parent.width
                    spacing: 8
                    Button {
                        objectName: "discoverPluginsAction"
                        text: qsTr("Discover plugins")
                        enabled: !!root.controller && !root.busy
                        palette.button: Style.themes.containColor
                        palette.buttonText: Style.themes.fontColor
                        onClicked: root.controller.discoverPlugins()
                    }
                }
            }
        }

        Rectangle {
            id: notice
            objectName: "pluginPanelNotice"
            anchors.top: header.bottom
            anchors.topMargin: 12
            width: parent.width
            height: noticeText.implicitHeight + 24
            radius: Style.settings.cubeRadius
            color: Style.themes.containColor
            border.color: Style.themes.sideColor
            border.width: 1

            Text {
                id: noticeText
                anchors.fill: parent
                anchors.margins: 12
                text: qsTr("Configure installed source plugins and their accounts. Loading a plugin does not enable an account.")
                color: Style.themes.textColor
                font.pixelSize: Style.settings.textmain
                wrapMode: Text.Wrap
            }
        }

        Item {
            id: workArea
            anchors.top: notice.bottom
            anchors.topMargin: 12
            anchors.bottom: parent.bottom
            width: parent.width

            Text {
                objectName: "pluginUnavailablePlaceholder"
                anchors.centerIn: parent
                width: parent.width
                visible: !root.controller
                text: qsTr("Plugin settings are unavailable")
                color: Style.themes.textColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "emptyPluginPlaceholder"
                anchors.centerIn: parent
                width: parent.width
                visible: !!root.controller && root.plugins.length === 0
                text: qsTr("No plugins discovered")
                color: Style.themes.textColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            ListView {
                id: masterList
                objectName: "pluginMasterList"
                visible: !!root.controller && root.plugins.length > 0
                x: 0
                y: 0
                width: root.compact ? workArea.width
                                    : Math.max(180, Math.min(280, workArea.width * 0.3))
                height: root.compact ? Math.min(180, workArea.height * 0.3) : workArea.height
                model: root.plugins
                spacing: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property var modelData
                    width: masterList.width
                    height: 60
                    radius: Style.settings.cubeRadius
                    color: modelData.id === (root.controller ? root.controller.selectedPluginId : "")
                           ? Style.themes.sideColor : Style.themes.containColor
                    border.color: Style.themes.sideColor
                    border.width: 1

                    Button {
                        objectName: "pluginSelect_" + modelData.id
                        anchors.fill: parent
                        palette.button: "transparent"
                        palette.buttonText: Style.themes.fontColor
                        background: Rectangle { color: "transparent" }
                        contentItem: Column {
                            spacing: 2
                            Text {
                                width: parent.width
                                text: modelData.name || modelData.id
                                color: Style.themes.fontColor
                                font.pixelSize: Style.settings.textmain
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: PluginText.pluginState(modelData.state)
                                color: Style.themes.textColor
                                font.pixelSize: Style.settings.textTip
                            }
                        }
                        onClicked: root.selectPlugin(modelData.id)
                    }
                }
            }

            ScrollView {
                id: detailPane
                objectName: "pluginDetailPane"
                visible: !!root.controller && root.plugins.length > 0
                x: root.compact ? 0 : masterList.width + 12
                y: root.compact ? masterList.height + 12 : 0
                width: root.compact ? workArea.width
                                    : Math.max(0, workArea.width - masterList.width - 12)
                height: root.compact ? Math.max(0, workArea.height - y) : workArea.height
                clip: true
                contentWidth: availableWidth
                contentHeight: detailColumn.implicitHeight + 12

                Column {
                    id: detailColumn
                    width: detailPane.availableWidth
                    spacing: 14

                    Rectangle {
                        width: parent.width
                        height: pluginSummary.implicitHeight + 24
                        radius: Style.settings.cubeRadius
                        color: Style.themes.containColor
                        border.color: Style.themes.sideColor
                        border.width: 1

                        Column {
                            id: pluginSummary
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4
                            Text {
                                width: parent.width
                                text: (root.selectedPlugin.name || root.selectedPlugin.id || qsTr("Plugin"))
                                      + (root.selectedPlugin.version ? " · " + root.selectedPlugin.version : "")
                                color: Style.themes.fontColor
                                font.pixelSize: Style.settings.textmain
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }
                            Text {
                                objectName: "pluginSummaryState"
                                width: parent.width
                                text: qsTr("State: %1").arg(PluginText.pluginState(root.selectedPlugin.state))
                                color: Style.themes.textColor
                                font.pixelSize: Style.settings.textTip
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: 8
                        Button {
                            objectName: "loadPluginAction"
                            visible: root.selectedPlugin.loadable === true
                            text: qsTr("Load")
                            enabled: visible && !root.busy
                            palette.button: Style.themes.containColor
                            palette.buttonText: Style.themes.fontColor
                            onClicked: root.controller.loadPlugin(root.selectedPlugin.id)
                        }
                        Button {
                            objectName: "unloadPluginAction"
                            visible: root.selectedPlugin.state === 2
                                     || root.selectedPlugin.state === "loaded"
                                     || root.selectedPlugin.unloadable === true
                            text: qsTr("Unload")
                            enabled: visible && root.selectedPlugin.unloadable === true && !root.busy
                            palette.button: Style.themes.containColor
                            palette.buttonText: Style.themes.fontColor
                            onClicked: {
                                schemaForm.clearSecrets()
                                root.controller.unloadPlugin(root.selectedPlugin.id)
                            }
                        }
                        Button {
                            objectName: "reloadPluginAction"
                            visible: root.selectedPlugin.reloadable === true
                            text: qsTr("Reload")
                            enabled: visible && !root.busy
                            palette.button: Style.themes.containColor
                            palette.buttonText: Style.themes.fontColor
                            onClicked: root.controller.reloadPlugin(root.selectedPlugin.id)
                        }
                    }

                    SourceAccountList {
                        width: parent.width
                        instances: root.controller ? root.controller.instances : []
                        selectedInstanceId: root.controller ? root.controller.selectedInstanceId : ""
                        busy: root.busy
                        onSelectInstanceRequested: (instanceId) => root.selectInstance(instanceId)
                        onNewInstanceRequested: root.selectInstance("")
                        onRemoveInstanceRequested: (instanceId) => {
                            root.pendingRemovalId = instanceId
                            removalConfirmation.open()
                        }
                        onEnabledRequested: (instanceId, enabled) => {
                            if (root.controller)
                                root.controller.setInstanceEnabled(instanceId, enabled)
                        }
                    }

                    TextField {
                        id: displayNameInput
                        objectName: "instanceDisplayNameField"
                        width: parent.width
                        placeholderText: qsTr("Account display name")
                        enabled: !root.busy
                        palette.base: Style.themes.containColor
                        palette.text: Style.themes.fontColor
                        palette.placeholderText: Style.themes.textColor
                        onTextEdited: root.probeCurrent = false
                    }

                    Text {
                        objectName: "noSchemaPlaceholder"
                        width: parent.width
                        visible: root.formSections.length === 0
                        text: qsTr("This plugin has no configurable settings")
                        color: Style.themes.textColor
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    SchemaSettingsForm {
                        id: schemaForm
                        width: parent.width
                        controller: root.controller
                        sections: root.formSections
                        busy: root.busy
                        onBrowseDirectoryRequested: (fieldId) => {
                            root.directoryFieldId = fieldId
                            directoryDialog.open()
                        }
                        onDraftEdited: root.probeCurrent = false
                    }

                    Flow {
                        objectName: "formActionArea"
                        width: parent.width
                        spacing: 8
                        Button {
                            objectName: "saveInstanceAction"
                            text: qsTr("Save")
                            enabled: !!root.controller && schemaForm.canSubmit
                            palette.button: Style.themes.themeColor
                            palette.buttonText: Style.themes.fontColor
                            onClicked: root.saveInstance()
                        }
                        Button {
                            objectName: "testConnectionAction"
                            text: qsTr("Test connection")
                            enabled: !!root.controller && schemaForm.canSubmit
                            palette.button: Style.themes.containColor
                            palette.buttonText: Style.themes.fontColor
                            onClicked: root.testConnection()
                        }
                        Button {
                            objectName: "cancelDraftAction"
                            text: qsTr("Cancel")
                            enabled: !!root.controller && !root.busy
                            palette.button: Style.themes.containColor
                            palette.buttonText: Style.themes.fontColor
                            onClicked: root.cancelDraft()
                        }
                    }

                    Text {
                        width: parent.width
                        visible: !!root.controller && root.controller.lastErrorKey
                        text: PluginText.error(root.controller ? root.controller.lastErrorKey : "")
                        color: "#d85a5a"
                        wrapMode: Text.Wrap
                        font.pixelSize: Style.settings.textTip
                    }

                    Column {
                        objectName: "settingsActionsArea"
                        width: parent.width
                        spacing: 6
                        visible: root.filteredActions().length > 0

                        Text {
                            width: parent.width
                            text: qsTr("Plugin actions")
                            color: Style.themes.fontColor
                            font.pixelSize: Style.settings.textmain
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: root.filteredActions()
                            delegate: Column {
                                required property var modelData
                                width: detailColumn.width
                                spacing: 3

                                Button {
                                    objectName: "settingsAction_" + modelData.id
                                    text: PluginText.translated(modelData.labelKey || "",
                                                                qsTr("Run action"))
                                    enabled: modelData.state === 1 && schemaForm.canSubmit
                                             && root.probeCurrent
                                    palette.button: Style.themes.containColor
                                    palette.buttonText: Style.themes.fontColor
                                    onClicked: root.requestAction(modelData)
                                }
                                Text {
                                    objectName: "settingsActionReason_" + modelData.id
                                    width: parent.width
                                    visible: modelData.state !== 1
                                    text: PluginText.availability(modelData.state,
                                                                  modelData.reasonKey || "")
                                    color: Style.themes.textColor
                                    font.pixelSize: Style.settings.textTip
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Column {
                        id: diagnostics
                        objectName: "sourceCapabilityDiagnostics"
                        width: parent.width
                        spacing: 8
                        visible: root.controller && root.controller.sourceCapabilities.length > 0

                        Text {
                            width: parent.width
                            text: qsTr("Source capabilities")
                            color: Style.themes.fontColor
                            font.pixelSize: Style.settings.textmain
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: root.controller ? root.controller.sourceCapabilities : []
                            delegate: Column {
                                required property var modelData
                                width: diagnostics.width
                                spacing: 3
                                Text {
                                    width: parent.width
                                    text: PluginText.actionName(modelData.action)
                                          + ": " + PluginText.availability(modelData.pluginState, "")
                                    color: Style.themes.textColor
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Style.settings.textTip
                                }
                                Text {
                                    objectName: "capabilityServer_" + modelData.action
                                    width: parent.width
                                    text: qsTr("Server: %1").arg(
                                              PluginText.layerAvailability(modelData.serverState))
                                    color: Style.themes.textColor
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Style.settings.textTip
                                }
                                Text {
                                    objectName: "capabilityAccount_" + modelData.action
                                    width: parent.width
                                    text: qsTr("Account: %1").arg(
                                              PluginText.layerAvailability(modelData.accountState))
                                    color: Style.themes.textColor
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Style.settings.textTip
                                }
                                Text {
                                    objectName: "capabilityEffective_" + modelData.action
                                    width: parent.width
                                    text: qsTr("Effective: %1").arg(
                                              PluginText.availability(modelData.state,
                                                                      modelData.reasonKey || ""))
                                    color: Style.themes.textColor
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Style.settings.textTip
                                }
                            }
                        }

                        Text {
                            objectName: "mediaPermissionDisclaimer"
                            width: parent.width
                            text: qsTr("These are source-level diagnostics, not per-media permission checks. Individual media may still be restricted.")
                            color: Style.themes.textColor
                            font.pixelSize: Style.settings.textTip
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
