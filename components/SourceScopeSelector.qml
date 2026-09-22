// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: root
    objectName: "sourceScopeSelector"
    property var modelObject: null
    model: modelObject ? modelObject.sourceOptions : []
    textRole: "displayName"
    valueRole: "sourceInstanceId"

    function synchronizeSelection() {
        if (!modelObject) {
            currentIndex = -1
            return
        }
        currentIndex = indexOfValue(modelObject.selectedSourceInstanceId)
    }

    onActivated: function(index) {
        if (!modelObject || index < 0 || index >= model.length)
            return
        var option = model[index]
        if (option.available === false) {
            synchronizeSelection()
            return
        }
        modelObject.selectedSourceInstanceId = option.sourceInstanceId
    }
    onModelChanged: Qt.callLater(synchronizeSelection)
    Component.onCompleted: synchronizeSelection()

    Connections {
        target: root.modelObject
        ignoreUnknownSignals: true
        function onSelectedSourceInstanceIdChanged() { root.synchronizeSelection() }
        function onSourceOptionsChanged() { root.synchronizeSelection() }
    }
}
