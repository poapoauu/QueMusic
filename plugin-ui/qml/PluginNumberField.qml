import QtQuick
import QueMusic.PluginUI 1.0

PluginTextField {
    id: root
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value: 0
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    validator: DoubleValidator { bottom: root.from; top: root.to }
    text: String(value)
    onValueChanged: {
        const bounded = Math.max(from, Math.min(to, value))
        if (value !== bounded)
            value = bounded
    }
    onEditingFinished: {
        const candidate = Number(text)
        value = Number.isFinite(candidate) ? Math.max(from, Math.min(to, candidate)) : from
        text = String(value)
    }
}
