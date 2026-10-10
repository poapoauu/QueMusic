import QtQuick
// Host-only bridge: the adapter and original Style are private Host types.
import QueMusic 1.0
import QueMusic.PluginUI 1.0

Item {
    id: root
    visible: false

    function synchronize() {
        PluginThemeAdapter.apply({
            background: Style.themes.fullColor,
            surface: Style.themes.primaryColor,
            surfaceHover: Style.themes.hoverColor,
            primary: Style.themes.themeColor,
            textPrimary: Style.themes.fontColor,
            textSecondary: Style.themes.textColor,
            border: Style.themes.borderColor,
            success: Style.darkis ? "#55d98a" : "#168447",
            warning: Style.darkis ? "#ffd166" : "#9a6500",
            danger: Style.darkis ? "#ff6b6b" : "#b42318",
            spacingSmall: 6,
            spacingMedium: 12,
            spacingLarge: 20,
            radiusSmall: 6,
            radiusMedium: 10,
            radiusLarge: 16,
            fontCaptionPixelSize: Style.settings.textTip,
            fontBodyPixelSize: Style.settings.text,
            fontTitlePixelSize: Style.settings.textH2,
            fontFamily: Style.settings.fontFamily,
            scaleFactor: 1.0,
            dark: Style.darkis,
            reducedMotion: !Style.settings.premiumAnime
        })
    }

    Component.onCompleted: synchronize()
    Connections {
        target: Style
        function onChangeTheme() { root.synchronize() }
        function onChangeUi() { root.synchronize() }
    }
}
