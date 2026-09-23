import QtQuick
import QueMusic.PluginUI 1.0

PluginTextField {
    validator: RegularExpressionValidator {
        regularExpression: /^https?:\/\/[^\s/]+(?:\/[^\s]*)?$/
    }
    inputMethodHints: Qt.ImhUrlCharactersOnly
}
