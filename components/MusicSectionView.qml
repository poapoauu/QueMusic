// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    property var hub: null
    property var modelObject: null
    property var playback: null
    property int pageKind: 0
    property bool aggregateMode: hub ? hub.selectedSourceInstanceId === "" : true
    readonly property bool busy: modelObject ? modelObject.state === 1 : false
    readonly property bool empty: !modelObject || modelObject.state === 3

    function sourceOption(instanceId) {
        var options = hub ? hub.sourceOptions : []
        for (var i = 0; i < options.length; ++i)
            if (options[i].sourceInstanceId === instanceId)
                return options[i]
        return { "sourceInstanceId": instanceId, "displayName": instanceId,
                 "available": false, "reasonKey": "source.instance.unavailable" }
    }

    function sameRef(left, right) {
        return left && right
            && left.sourcePluginId === right.sourcePluginId
            && left.sourceInstanceId === right.sourceInstanceId
            && left.accountId === right.accountId
            && left.entityType === right.entityType
            && left.entityId === right.entityId
    }

    function activateItem(item) {
        if (!item || !item.ref)
            return
        if (item.ref.entityType === 0) {
            if (playback) playback.play(item)
        } else if (hub && item.ref.entityType >= 1 && item.ref.entityType <= 4) {
            hub.browse(item)
        }
    }

    ListView {
        id: sections
        objectName: "musicSections"
        anchors.fill: parent
        clip: true
        spacing: 18
        model: root.modelObject

        delegate: Column {
            id: sectionDelegate
            required property int index
            required property string sectionId
            required property string title
            required property string layoutHint
            required property var items
            required property bool hasMore
            required property bool loadingMore
            required property var error
            width: sections.width
            spacing: 8

            Label { text: sectionDelegate.title; font.bold: true }

            Grid {
                id: itemGrid
                objectName: "sectionItems"
                width: sectionDelegate.width
                columns: sectionDelegate.layoutHint === "horizontal"
                    ? Math.max(1, Math.floor(width / 280)) : 1
                columnSpacing: 8
                rowSpacing: 8

                Repeater {
                    model: sectionDelegate.items
                    delegate: ItemDelegate {
                    id: mediaDelegate
                    required property var modelData
                    objectName: "mediaItem"
                    width: (itemGrid.width - (itemGrid.columns - 1) * itemGrid.columnSpacing)
                        / itemGrid.columns
                    property var fullItem: modelData
                    property var artworkRequestId: null
                    property url artworkUrl: ""
                    property var sourceInfo: root.sourceOption(fullItem.ref.sourceInstanceId)
                    text: fullItem.title + (fullItem.subtitle ? " · " + fullItem.subtitle : "")
                    onClicked: root.activateItem(fullItem)

                    Label {
                        objectName: "sourceBadge"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: mediaDelegate.sourceInfo.displayName
                        opacity: root.aggregateMode ? 1.0 : 0.65
                    }

                    Component.onCompleted: {
                        if (root.hub && fullItem.artworkId && artworkUrl.toString() === "")
                            artworkRequestId = root.hub.loadArtwork(fullItem.ref)
                    }
                    Component.onDestruction: {
                        if (root.hub && artworkRequestId)
                            root.hub.cancelAsset(artworkRequestId)
                    }
                    Connections {
                        target: root.hub
                        ignoreUnknownSignals: true
                        function onArtworkReady(requestId, media, localUrl) {
                            if (requestId === mediaDelegate.artworkRequestId
                                    && root.sameRef(media, mediaDelegate.fullItem.ref)) {
                                mediaDelegate.artworkUrl = localUrl
                                mediaDelegate.artworkRequestId = null
                            }
                        }
                        function onAssetFailed(requestId) {
                            if (requestId === mediaDelegate.artworkRequestId)
                                mediaDelegate.artworkRequestId = null
                        }
                    }
                }
                }
            }

            Button {
                objectName: "sectionRetry"
                visible: Object.keys(sectionDelegate.error || {}).length > 0
                text: qsTr("重试")
                onClicked: if (root.hub) root.hub.retrySection(root.pageKind, sectionDelegate.sectionId)
            }
            Button {
                objectName: "sectionLoadMore"
                visible: sectionDelegate.hasMore
                enabled: !sectionDelegate.loadingMore
                text: sectionDelegate.loadingMore ? qsTr("加载中") : qsTr("加载更多")
                onClicked: if (root.hub) root.hub.loadMore(root.pageKind, sectionDelegate.sectionId)
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: root.empty
        text: qsTr("暂无内容")
    }
    BusyIndicator { anchors.centerIn: parent; running: root.busy && sections.count === 0 }
}
