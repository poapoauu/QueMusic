// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: root
    property var hub: null
    property var item: ({})
    property url downloadDestination: ""
    property int rating: 5
    property real bookmarkPosition: 0
    property var playlistContext: null

    function availability(action) {
        var actions = item && item.availableActions ? item.availableActions : ({})
        return actions[String(action)] || { "state": 0, "reasonKey": "", "constraints": ({}) }
    }
    function shown(action) { return availability(action).state !== 0 }
    function allowed(action) { return availability(action).state === 1 }
    function reason(action) { return availability(action).reasonKey || "" }
    function router() { return hub ? hub.actions : null }
    function sameSourcePlaylistContext() {
        if (!playlistContext || !playlistContext.playlist || !playlistContext.playlist.ref
                || !item || !item.ref)
            return false
        return playlistContext.playlist.ref.sourcePluginId === item.ref.sourcePluginId
            && playlistContext.playlist.ref.sourceInstanceId === item.ref.sourceInstanceId
            && playlistContext.playlist.ref.accountId === item.ref.accountId
            && playlistContext.playlist.ref.entityType === 3
            && item.ref.entityType === 0
    }
    function playlistReason(action) {
        if (allowed(action) && (!sameSourcePlaylistContext()
                || (action === 12 && !hasValidRemovalIndexes())))
            return "music.playlistSelectionRequired"
        return reason(action)
    }
    function hasValidRemovalIndexes() {
        if (!playlistContext || !playlistContext.trackIndexesToRemove
                || playlistContext.trackIndexesToRemove.length === 0)
            return false
        var seen = ({})
        for (var i = 0; i < playlistContext.trackIndexesToRemove.length; ++i) {
            var index = playlistContext.trackIndexesToRemove[i]
            if (!Number.isInteger(index) || index < 0 || seen[String(index)])
                return false
            seen[String(index)] = true
        }
        return true
    }

    MenuItem {
        id: downloadAction
        objectName: "downloadAction"
        property string reasonKey: root.reason(3)
        visible: root.shown(3)
        enabled: root.allowed(3) && root.downloadDestination.toString() !== ""
        text: qsTr("下载")
        onTriggered: if (root.router()) root.router().download(root.item, root.downloadDestination)
    }
    MenuItem {
        objectName: "favoriteAction"
        property string reasonKey: root.reason(4)
        visible: root.shown(4)
        enabled: root.allowed(4)
        text: qsTr("收藏")
        onTriggered: if (root.router()) root.router().setFavorite(root.item, true)
    }
    MenuItem {
        objectName: "unfavoriteAction"
        property string reasonKey: root.reason(5)
        visible: root.shown(5)
        enabled: root.allowed(5)
        text: qsTr("取消收藏")
        onTriggered: if (root.router()) root.router().setFavorite(root.item, false)
    }
    MenuItem {
        objectName: "ratingAction"
        property string reasonKey: root.reason(6)
        visible: root.shown(6)
        enabled: root.allowed(6)
        text: qsTr("评分")
        onTriggered: if (root.router()) root.router().setRating(root.item, root.rating)
    }
    MenuItem {
        objectName: "addPlaylistAction"
        property string reasonKey: root.playlistReason(11)
        visible: root.shown(11)
        enabled: root.allowed(11) && root.sameSourcePlaylistContext()
        text: qsTr("加入歌单")
        onTriggered: if (root.router()) root.router().updatePlaylist(
            root.playlistContext.playlist, { "tracksToAdd": [root.item] })
    }
    MenuItem {
        objectName: "removePlaylistAction"
        property string reasonKey: root.playlistReason(12)
        visible: root.shown(12)
        enabled: root.allowed(12) && root.sameSourcePlaylistContext()
            && root.hasValidRemovalIndexes()
        text: qsTr("移出歌单")
        onTriggered: if (root.router()) root.router().updatePlaylist(
            root.playlistContext.playlist,
            { "trackIndexesToRemove": root.playlistContext.trackIndexesToRemove })
    }
    MenuItem {
        objectName: "bookmarkAction"
        property string reasonKey: root.reason(16)
        visible: root.shown(16)
        enabled: root.allowed(16)
        text: qsTr("保存书签")
        onTriggered: if (root.router()) root.router().setBookmark(root.item, root.bookmarkPosition)
    }
}
