import QtQuick
import QueMusic.PluginUI 1.0

PluginPage {
    title: "External fixture"
    PluginSection {
        title: "Connection"
        PluginTextField { placeholderText: "Server URL" }
        PluginPrimaryButton { text: "Connect" }
    }
}
