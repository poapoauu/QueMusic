// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "qrc:/QueMusic/pages"

Rectangle {
    id: mainContent
    objectName: "mainContent"
    color: "transparent"
    property var musicHub: null
    property var playbackCoordinator: null
    readonly property int pageHeight: Math.max(0, height - 60)
    property bool pageLoadingEnabled: true
    property int pageIndex: 0
    property var pages: [homePage, categoryPage, null, favoritesPage,
                         filePage, downloadPage, searchPage]

    function contentIndexed(choice) {
        if (!Number.isInteger(choice) || choice < 0 || choice >= pages.length
                || pages[choice] === null)
            return false
        if (choice === pageIndex) return true
        var previous = pages[pageIndex]
        if (previous) {
            previous.visible = false
            previous.active = false
        }
        var target = pages[choice]
        target.active = pageLoadingEnabled
        target.visible = true
        pageIndex = choice
        return true
    }

    Loader {
        id: homePage
        anchors.fill: parent; anchors.topMargin: 60
        active: mainContent.pageLoadingEnabled; visible: true
        sourceComponent: HomePage {
            hub: mainContent.musicHub; playback: mainContent.playbackCoordinator
            pageActive: homePage.active && homePage.visible
        }
    }
    Loader {
        id: categoryPage
        anchors.fill: parent; anchors.topMargin: 60
        active: false; visible: false
        sourceComponent: PlaylistPage {
            hub: mainContent.musicHub; playback: mainContent.playbackCoordinator
            pageActive: categoryPage.active && categoryPage.visible
        }
    }
    Loader {
        id: favoritesPage
        anchors.fill: parent; anchors.topMargin: 60
        active: false; visible: false
        sourceComponent: FavouritePage {
            hub: mainContent.musicHub; playback: mainContent.playbackCoordinator
            pageActive: favoritesPage.active && favoritesPage.visible
        }
    }
    Loader {
        id: filePage
        anchors.fill: parent; anchors.topMargin: 60
        active: false; visible: false
        source: "qrc:/QueMusic/pages/FilePage.qml"
    }
    Loader {
        id: downloadPage
        anchors.fill: parent; anchors.topMargin: 60
        active: false; visible: false
        source: "qrc:/QueMusic/pages/DownloadPage.qml"
    }
    Loader {
        id: searchPage
        anchors.fill: parent; anchors.topMargin: 60
        active: false; visible: false
        sourceComponent: SearchPage {
            hub: mainContent.musicHub; playback: mainContent.playbackCoordinator
            pageActive: searchPage.active && searchPage.visible
        }
    }
}
