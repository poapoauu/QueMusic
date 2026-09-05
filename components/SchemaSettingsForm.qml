// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors

import QtQuick
import QtQuick.Controls.Basic
import QueMusic 1.0
import "PluginSettingsText.js" as PluginText

Column {
    id: root
    objectName: "schemaSettingsForm"

    property var controller: null
    property var sections: []
    property bool busy: false
    property var publicDraft: ({})
    property bool synchronizationFailed: false
    property bool fieldsValid: true
    readonly property bool canSubmit: !busy && fieldsValid && !synchronizationFailed
    property var _secretInputs: []
    property var _publicOverrides: ({})
    property var _fieldRows: []
    property var _sectionRows: []
    property var renderSections: []

    signal browseDirectoryRequested(string fieldId)
    signal draftEdited()

    spacing: 14

    function isSecret(fieldData) {
        return fieldData.type === 1 || fieldData.secret === true
    }

    function initialPublicDraft(sectionData) {
        var draft = {}
        var source = sectionData || []
        for (var sectionIndex = 0; sectionIndex < source.length; ++sectionIndex) {
            var fields = source[sectionIndex].fields || []
            for (var fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
                var item = fields[fieldIndex]
                if (!isSecret(item))
                    draft[item.id] = item.value
            }
        }
        return draft
    }

    function resetFromSections(sectionData) {
        clearSecrets()
        sections = sectionData || []
        renderSections = sections
        publicDraft = initialPublicDraft(sections)
        _publicOverrides = ({})
        synchronizationFailed = false
        revalidate()
    }

    function structureSignature(sectionData) {
        var result = []
        var source = sectionData || []
        for (var sectionIndex = 0; sectionIndex < source.length; ++sectionIndex) {
            var ids = []
            var fields = source[sectionIndex].fields || []
            for (var fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex)
                ids.push(fields[fieldIndex].id)
            result.push(String(source[sectionIndex].id) + ":" + ids.join(","))
        }
        return result.join("|")
    }

    function mergeFromSections(sectionData) {
        var incoming = sectionData || []
        if (structureSignature(incoming) !== structureSignature(renderSections)) {
            resetFromSections(incoming)
            return
        }
        sections = incoming
        var nextDraft = {}
        for (var key in publicDraft)
            nextDraft[key] = publicDraft[key]
        for (var sectionIndex = 0; sectionIndex < incoming.length; ++sectionIndex) {
            var fields = incoming[sectionIndex].fields || []
            for (var fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
                var item = fields[fieldIndex]
                if (!isSecret(item) && !_publicOverrides[item.id]
                    && Object.prototype.hasOwnProperty.call(item, "value"))
                    nextDraft[item.id] = item.value
            }
        }
        publicDraft = nextDraft
        for (var rowIndex = 0; rowIndex < _sectionRows.length; ++rowIndex) {
            var sectionRow = _sectionRows[rowIndex]
            for (var newSectionIndex = 0; newSectionIndex < incoming.length; ++newSectionIndex) {
                if (incoming[newSectionIndex].id === sectionRow.modelData.id) {
                    sectionRow.presentedTitleKey = incoming[newSectionIndex].titleKey || ""
                    break
                }
            }
        }
        for (var fieldRowIndex = 0; fieldRowIndex < _fieldRows.length; ++fieldRowIndex) {
            var fieldRow = _fieldRows[fieldRowIndex]
            for (var incomingSectionIndex = 0; incomingSectionIndex < incoming.length; ++incomingSectionIndex) {
                var incomingFields = incoming[incomingSectionIndex].fields || []
                for (var incomingFieldIndex = 0; incomingFieldIndex < incomingFields.length; ++incomingFieldIndex) {
                    if (incomingFields[incomingFieldIndex].id === fieldRow.fieldData.id)
                        fieldRow.fieldData = incomingFields[incomingFieldIndex]
                }
            }
        }
        revalidate()
    }

    function updatePublic(fieldId, value) {
        var next = {}
        for (var key in publicDraft)
            next[key] = publicDraft[key]
        next[fieldId] = value
        publicDraft = next
        var overrides = {}
        for (var key in _publicOverrides)
            overrides[key] = _publicOverrides[key]
        overrides[fieldId] = true
        _publicOverrides = overrides
        synchronizationFailed = false
        revalidate()
        draftEdited()
    }

    function valueFor(fieldData) {
        return Object.prototype.hasOwnProperty.call(publicDraft, fieldData.id)
                ? publicDraft[fieldData.id] : fieldData.value
    }

    function validUrl(value) {
        return /^https?:\/\/(?![^\/?#]*@)(?:\[[0-9a-f:]+\]|[a-z0-9](?:[a-z0-9-]*[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]*[a-z0-9])?)*)(?::[0-9]+)?(?:[\/?#].*)?$/i
                .test(String(value || ""))
    }

    function revalidate() {
        var okay = true
        for (var sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
            var fields = sections[sectionIndex].fields || []
            for (var fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
                var item = fields[fieldIndex]
                if (item.visible === false)
                    continue
                if (isSecret(item)) {
                    if (item.required && !item.credentialConfigured) {
                        var secretControl = secretInput(item.id)
                        if (!secretControl || !secretControl.text)
                            okay = false
                    }
                    continue
                }
                var value = valueFor(item)
                if (item.required && (value === undefined || value === null || String(value).length === 0))
                    okay = false
                if (item.type === 2 && String(value || "").length > 0 && !validUrl(value))
                    okay = false
                var constraints = item.constraints || {}
                if (constraints.minLength !== undefined
                    && String(value || "").length < Number(constraints.minLength))
                    okay = false
                if (constraints.maxLength !== undefined
                    && String(value || "").length > Number(constraints.maxLength))
                    okay = false
                if (constraints.pattern
                    && !(new RegExp(constraints.pattern)).test(String(value || "")))
                    okay = false
            }
        }
        fieldsValid = okay
    }

    function registerSecret(fieldId, control) {
        var next = _secretInputs.slice(0)
        next.push({ fieldId: fieldId, control: control })
        _secretInputs = next
        revalidate()
    }

    function registerFieldRow(control) {
        var next = _fieldRows.slice(0)
        next.push(control)
        _fieldRows = next
    }

    function unregisterFieldRow(control) {
        _fieldRows = _fieldRows.filter(function(item) { return item !== control })
    }

    function registerSectionRow(control) {
        var next = _sectionRows.slice(0)
        next.push(control)
        _sectionRows = next
    }

    function unregisterSectionRow(control) {
        _sectionRows = _sectionRows.filter(function(item) { return item !== control })
    }

    function unregisterSecret(control) {
        var next = []
        for (var index = 0; index < _secretInputs.length; ++index) {
            if (_secretInputs[index].control !== control)
                next.push(_secretInputs[index])
        }
        _secretInputs = next
    }

    function secretInput(fieldId) {
        for (var index = 0; index < _secretInputs.length; ++index) {
            if (_secretInputs[index].fieldId === fieldId)
                return _secretInputs[index].control
        }
        return null
    }

    function collectSecretDraft() {
        var result = {}
        for (var index = 0; index < _secretInputs.length; ++index) {
            var entry = _secretInputs[index]
            if (entry.control && entry.control.text)
                result[entry.fieldId] = entry.control.text
        }
        return result
    }

    function clearSecrets() {
        for (var index = 0; index < _secretInputs.length; ++index) {
            if (_secretInputs[index].control)
                _secretInputs[index].control.text = ""
        }
        revalidate()
    }

    function synchronizePublicDraft() {
        if (!controller || !canSubmit)
            return false
        controller.cancelOperation()
        var accepted = controller.setDraftValues(publicDraft)
        synchronizationFailed = !accepted
        if (accepted)
            _publicOverrides = ({})
        return accepted
    }

    function acceptControllerValue(fieldId) {
        var overrides = {}
        for (var key in _publicOverrides) {
            if (key !== fieldId)
                overrides[key] = _publicOverrides[key]
        }
        _publicOverrides = overrides
    }

    Repeater {
        model: root.renderSections || []

        delegate: Column {
            id: sectionRow
            required property var modelData
            property var sectionData: modelData
            property string presentedTitleKey: modelData.titleKey || ""
            width: root.width
            spacing: 10
            Component.onCompleted: root.registerSectionRow(this)
            Component.onDestruction: root.unregisterSectionRow(this)

            Text {
                width: parent.width
                text: PluginText.translated(sectionRow.presentedTitleKey, qsTr("Settings"))
                color: Style.themes.fontColor
                font.pixelSize: Style.settings.textmain
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Repeater {
                model: sectionRow.modelData.fields || []

                delegate: Column {
                    id: fieldRow
                    required property var modelData
                    property var fieldData: modelData
                    width: parent.width
                    visible: fieldData.visible !== false
                    height: visible ? implicitHeight : 0
                    spacing: 4
                    Component.onCompleted: root.registerFieldRow(this)
                    Component.onDestruction: root.unregisterFieldRow(this)

                    Text {
                        objectName: "fieldLabel_" + fieldRow.fieldData.id
                        property string presentationKey: fieldRow.fieldData.labelKey || ""
                        width: parent.width
                        text: PluginText.translated(fieldRow.fieldData.labelKey || "",
                                                    fieldRow.fieldData.id)
                              + (fieldRow.fieldData.required ? " *" : "")
                        color: Style.themes.textColor
                        font.pixelSize: Style.settings.textTip
                        wrapMode: Text.Wrap
                    }

                    Loader {
                        id: editorLoader
                        width: parent.width
                        active: fieldRow.visible
                        sourceComponent: root.isSecret(fieldRow.fieldData) ? secretEditor
                                         : fieldRow.fieldData.type === 3 ? integerEditor
                                         : fieldRow.fieldData.type === 4 ? booleanEditor
                                         : fieldRow.fieldData.type === 5 ? choiceEditor
                                         : fieldRow.fieldData.type === 6 ? directoryEditor
                                         : textEditor
                    }

                    Component {
                        id: textEditor
                        TextField {
                            objectName: "field_" + fieldRow.fieldData.id
                            width: fieldRow.width
                            text: String(root.valueFor(fieldRow.fieldData) || "")
                            echoMode: TextInput.Normal
                            palette.base: Style.themes.containColor
                            palette.text: Style.themes.fontColor
                            palette.placeholderText: Style.themes.textColor
                            onTextEdited: root.updatePublic(fieldRow.fieldData.id, text)
                        }
                    }

                    Component {
                        id: secretEditor
                        TextField {
                            objectName: "field_" + fieldRow.fieldData.id
                            width: fieldRow.width
                            text: ""
                            placeholderText: PluginText.configuredSecretPlaceholder(
                                                 fieldRow.fieldData.credentialConfigured === true)
                            echoMode: TextInput.Password
                            palette.base: Style.themes.containColor
                            palette.text: Style.themes.fontColor
                            palette.placeholderText: Style.themes.textColor
                            onTextEdited: {
                                root.synchronizationFailed = false
                                root.revalidate()
                                root.draftEdited()
                            }
                            Component.onCompleted: root.registerSecret(fieldRow.fieldData.id, this)
                            Component.onDestruction: root.unregisterSecret(this)
                        }
                    }

                    Component {
                        id: integerEditor
                        SpinBox {
                            objectName: "field_" + fieldRow.fieldData.id
                            width: fieldRow.width
                            editable: true
                            from: !fieldRow.fieldData.constraints
                                  || fieldRow.fieldData.constraints.min === undefined
                                  ? -2147483647 : fieldRow.fieldData.constraints.min
                            to: !fieldRow.fieldData.constraints
                                || fieldRow.fieldData.constraints.max === undefined
                                ? 2147483647 : fieldRow.fieldData.constraints.max
                            value: Number(root.valueFor(fieldRow.fieldData) || 0)
                            palette.base: Style.themes.containColor
                            palette.text: Style.themes.fontColor
                            onValueModified: root.updatePublic(fieldRow.fieldData.id, value)
                        }
                    }

                    Component {
                        id: booleanEditor
                        Switch {
                            objectName: "field_" + fieldRow.fieldData.id
                            checked: Boolean(root.valueFor(fieldRow.fieldData))
                            text: checked ? qsTr("Enabled") : qsTr("Disabled")
                            palette.buttonText: Style.themes.fontColor
                            palette.text: Style.themes.fontColor
                            onClicked: root.updatePublic(fieldRow.fieldData.id, checked)
                        }
                    }

                    Component {
                        id: choiceEditor
                        ComboBox {
                            id: choiceControl
                            objectName: "field_" + fieldRow.fieldData.id
                            width: fieldRow.width
                            model: fieldRow.fieldData.choices || []
                            currentIndex: {
                                var wanted = root.valueFor(fieldRow.fieldData)
                                for (var index = 0; index < model.length; ++index) {
                                    if (model[index] === wanted)
                                        return index
                                }
                                return model.length > 0 ? 0 : -1
                            }
                            palette.button: Style.themes.containColor
                            palette.buttonText: Style.themes.fontColor
                            palette.text: Style.themes.fontColor
                            onActivated: root.updatePublic(fieldRow.fieldData.id, model[currentIndex])
                        }
                    }

                    Component {
                        id: directoryEditor
                        Row {
                            width: fieldRow.width
                            spacing: 8
                            TextField {
                                objectName: "field_" + fieldRow.fieldData.id
                                width: Math.max(80, parent.width - browseButton.width - parent.spacing)
                                text: String(root.valueFor(fieldRow.fieldData) || "")
                                palette.base: Style.themes.containColor
                                palette.text: Style.themes.fontColor
                                palette.placeholderText: Style.themes.textColor
                                onTextEdited: root.updatePublic(fieldRow.fieldData.id, text)
                            }
                            Button {
                                id: browseButton
                                objectName: "directoryBrowse_" + fieldRow.fieldData.id
                                text: qsTr("Choose folder")
                                enabled: !root.busy
                                palette.button: Style.themes.containColor
                                palette.buttonText: Style.themes.fontColor
                                onClicked: root.browseDirectoryRequested(fieldRow.fieldData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        visible: root.synchronizationFailed
        width: parent.width
        text: qsTr("The visible draft is invalid and was not submitted")
        color: "#d85a5a"
        wrapMode: Text.Wrap
        font.pixelSize: Style.settings.textTip
    }
}
