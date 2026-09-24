import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QueMusic.PluginUI 1.0

Column {
    id: root
    property var settings: null
    property var host: null
    property var sections: settings ? settings.sections : []
    property var draft: ({})
    property bool busy: false
    property string errorKey: ""
    property string directoryField: ""
    signal cancelled()
    signal saved()
    spacing: PluginTheme.spacingMedium
    width: parent ? parent.width : implicitWidth

    function setPublicValue(fieldId, value) {
        var next = Object.assign({}, draft)
        next[fieldId] = value
        draft = next
    }
    function savePublic() {
        if (!settings || busy) return
        busy = true
        errorKey = ""
        settings.savePublicValues(draft)
    }
    function cancel() {
        draft = ({})
        errorKey = ""
        cancelled()
    }

    Connections {
        target: root.settings
        function onOperationFinished(requestId, success, reasonKey) {
            root.busy = false
            root.errorKey = success ? "" : reasonKey
            if (success) root.saved()
        }
        function onChanged() { root.sections = root.settings.sections }
    }
    Connections {
        target: root.host
        function onDirectorySelected(requestId, localDirectory) {
            if (root.directoryField && localDirectory.toString().startsWith("file:"))
                root.setPublicValue(root.directoryField,
                                    decodeURIComponent(String(localDirectory).replace(/^file:\/\//, "")))
            root.directoryField = ""
        }
    }

    Repeater {
        model: root.sections
        delegate: Column {
            id: section
            required property var modelData
            width: root.width
            spacing: PluginTheme.spacingSmall
            PluginLabel { text: section.modelData.titleKey || "" }
            Repeater {
                model: section.modelData.fields || []
                delegate: Column {
                    id: fieldRow
                    required property var modelData
                    width: section.width
                    spacing: PluginTheme.spacingSmall
                    visible: modelData.visible !== false
                    PluginLabel { text: fieldRow.modelData.labelKey || fieldRow.modelData.id }
                    PluginPasswordField {
                        id: secretInput
                        objectName: "pluginSettingsSecretInput"
                        width: fieldRow.width
                        visible: fieldRow.modelData.secret
                        enabled: !root.busy
                        Accessible.name: fieldRow.modelData.labelKey || fieldRow.modelData.id
                    }
                    RowLayout {
                        visible: fieldRow.modelData.secret
                        PluginButton {
                            text: "Save credential"
                            enabled: !root.busy && secretInput.text.length > 0
                            onClicked: {
                                var value = secretInput.text
                                secretInput.text = ""
                                root.busy = true
                                root.settings.saveSecret(fieldRow.modelData.id, value)
                            }
                        }
                        PluginButton {
                            text: "Clear credential"
                            enabled: !root.busy && fieldRow.modelData.credentialConfigured
                            onClicked: {
                                root.busy = true
                                root.settings.clearSecret(fieldRow.modelData.id)
                            }
                        }
                    }
                    PluginTextField {
                        width: fieldRow.width
                        visible: !fieldRow.modelData.secret && [0, 2, 6].includes(fieldRow.modelData.type)
                        enabled: !root.busy && fieldRow.modelData.type !== 6
                        text: root.draft[fieldRow.modelData.id] !== undefined
                            ? String(root.draft[fieldRow.modelData.id])
                            : String(root.settings ? root.settings.publicValues[fieldRow.modelData.id] || "" : "")
                        onTextEdited: root.setPublicValue(fieldRow.modelData.id, text)
                        Accessible.name: fieldRow.modelData.labelKey || fieldRow.modelData.id
                    }
                    PluginButton {
                        text: "Browse"
                        visible: fieldRow.modelData.type === 6
                        enabled: !root.busy && !!root.host
                        onClicked: {
                            root.directoryField = fieldRow.modelData.id
                            root.host.requestDirectory()
                        }
                    }
                    PluginNumberField {
                        visible: fieldRow.modelData.type === 3
                        enabled: !root.busy
                        value: Number(root.draft[fieldRow.modelData.id] !== undefined
                            ? root.draft[fieldRow.modelData.id]
                            : root.settings ? root.settings.publicValues[fieldRow.modelData.id] || 0 : 0)
                        onValueChanged: {
                            if (visible) root.setPublicValue(fieldRow.modelData.id, value)
                        }
                    }
                    PluginSwitch {
                        visible: fieldRow.modelData.type === 4
                        enabled: !root.busy
                        checked: Boolean(root.draft[fieldRow.modelData.id] !== undefined
                            ? root.draft[fieldRow.modelData.id]
                            : root.settings && root.settings.publicValues[fieldRow.modelData.id])
                        onToggled: root.setPublicValue(fieldRow.modelData.id, checked)
                    }
                    PluginComboBox {
                        visible: fieldRow.modelData.type === 5
                        enabled: !root.busy
                        model: fieldRow.modelData.choices || []
                        onActivated: root.setPublicValue(fieldRow.modelData.id, currentText)
                    }
                }
            }
        }
    }
    PluginDescription { text: root.errorKey; visible: root.errorKey.length > 0 }
    RowLayout {
        PluginPrimaryButton { text: "Save"; enabled: !root.busy; onClicked: root.savePublic() }
        PluginButton { text: "Cancel"; enabled: !root.busy; onClicked: root.cancel() }
        PluginBusyIndicator { running: root.busy; visible: root.busy }
    }
}
