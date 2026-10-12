import QtQuick
import QueMusic.PluginUI 1.0
import "qrcode.js" as QrEncoder

Item {
    id: root
    property url source: ""
    // Offline byte-mode payload; printable ASCII only, at most version 10/M.
    // Non-empty payload takes precedence over source, including on failure.
    property string payload: ""
    readonly property var modules: {
        if (payload.length === 0 || payload.length > 213 || /[^\x20-\x7e]/.test(payload))
            return []
        try { return QrEncoder.qrcode(payload, "M") } catch (_) { return [] }
    }
    readonly property bool validPayload: modules.length > 0
    readonly property bool payloadFits: validPayload && Math.floor(Math.min(width, height) / (modules.length + 8)) >= 1
    readonly property bool validSource: {
        const value = source.toString()
        const packageRelative = value.length > 0
            && !/^[A-Za-z][A-Za-z0-9+.-]*:/.test(value)
            && !value.startsWith("/")
            && value.split("/").indexOf("..") === -1
        return packageRelative || value.startsWith("qrc:/")
            || value.startsWith("file:/") || value.startsWith("image://")
    }
    readonly property bool error: payload.length > 0 ? !validPayload || !payloadFits
                                 : !validSource || qrImage.status === Image.Error
    implicitWidth: 180 * PluginTheme.scaleFactor
    implicitHeight: implicitWidth
    Accessible.name: error ? qsTr("QR code unavailable") : qsTr("QR code")

    Image {
        id: qrImage
        objectName: "pluginQrImage"
        anchors.fill: parent
        source: root.payload.length === 0 && root.validSource ? root.source : ""
        fillMode: Image.PreserveAspectFit
        visible: root.payload.length === 0 && !root.error
    }
    Canvas {
        id: code
        objectName: "pluginQrCanvas"
        anchors.centerIn: parent
        width: Math.floor(Math.min(root.width, root.height))
        height: width
        visible: root.payload.length > 0 && !root.error
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.validPayload) return
            const count = root.modules.length
            const cell = Math.floor(width / (count + 8))
            if (cell < 1) return
            const offset = Math.floor((width - cell * (count + 8)) / 2) + cell * 4
            // QR optical contrast and four-module quiet zone are fixed, not
            // theme colours. The surrounding surface still follows the Kit.
            ctx.fillStyle = "white"
            ctx.fillRect(0, 0, width, height)
            ctx.fillStyle = "black"
            for (let row = 0; row < count; ++row)
                for (let col = 0; col < count; ++col)
                    if (root.modules[row][col])
                        ctx.fillRect(offset + col * cell, offset + row * cell, cell, cell)
        }
        Connections {
            target: root
            function onModulesChanged() { code.requestPaint() }
        }
    }
    PluginErrorState {
        anchors.centerIn: parent
        title: qsTr("QR code unavailable")
        visible: root.error
    }
}
