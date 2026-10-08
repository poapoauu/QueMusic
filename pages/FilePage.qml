// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 QueMusic Contributors
//
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QueMusic 1.0
import 'qrc:/QueMusic/components'
Item {
    id: filePage

    property var musicAdapter: null
    property var legacyMigrationCandidates: []
    property var legacyMigrationPreview: ({})
    function legacySongStatus(folderId, songId) {
        if (typeof legacyCollectionMigration === "undefined") return "notMigrated";
        var revision = legacyCollectionMigration.revision;
        return legacyCollectionMigration.songStatus(folderId, songId);
    }
    function legacySongBlocked(status) {
        return status !== "matched";
    }
    signal requestPluginSettings(string packageId, string instanceId)
    function directoryRow(view, index) {
        return view && view.model && typeof view.model.get === "function"
                && index >= 0 && index < view.model.count ? view.model.get(index) : null;
    }
    function directoryCapabilities(row) {
        return musicAdapter && row && typeof musicAdapter.capabilities === "function"
                ? musicAdapter.capabilities(row) : ({});
    }
    function requestDirectorySections(retry) {
        if (!musicAdapter || musicAdapter.directoryState === "loading") return;
        var rows = musicAdapter.directoryItems;
        if (!rows) return;
        var ids = retry ? rows.retrySectionIds : rows.paginationSectionIds;
        var action = retry ? "retryDirectorySection" : "loadMoreDirectories";
        if (ids === undefined || typeof musicAdapter[action] !== "function") return;
        // Requests can synchronously reset presentation rows and section state.
        var targets = Array.from(ids);
        for (var id of targets)
            if (id) musicAdapter[action](id);
        // A whole-page failure has no section origin to retry.
        if (retry && targets.length === 0 && Object.keys(rows.error || {}).length === 0
                && musicAdapter.directoryState === "failed") musicAdapter.refreshDirectories();
    }
    function directoryHasError() {
        var rows = musicAdapter ? musicAdapter.directoryItems : null;
        if (!rows) return false;
        return Object.keys(rows.error || {}).length > 0 || musicAdapter.directoryState === "failed";
    }
    Connections {
        target: window
        function onExit() {
            if (!filePage.musicAdapter) return;
            for (var depth = 0; depth < 32 && filePage.musicAdapter.directoryCanNavigateBack; ++depth)
                if (!filePage.musicAdapter.directoryBack()) break;
        }
    }

    property int folderNumber: 0
    property int setMode: 0
    property list<int> chooseIndex: []
    signal loaded()
    signal cancelChoose()

    // 首页面
    Item {
        id: fileMain
        x: 0
        y: 0
        width: filePage.width
        height: filePage.height
        visible: true

        // 顶部标题
        Item {
            x: 24
            y: 24
            height: 40
            width: fileMain.width - 48
            z: 10
            Text {
                x: 0
                y: 0
                height: 40
                verticalAlignment: Text.AlignVCenter
                text: "本地音乐"
                font.weight: Font.DemiBold
                font.pixelSize: Style.settings.pageTitle
                color: Style.themes.fontColor
            }
        }

        QBlurTapBar {
            objectName: "localDirectoryTabs"
            x: 24
            y: 80
            z: 5
            model: ["我的文件夹","本地文件夹"]
            tabWidth: 120
            width: 244
            rectXy: Qt.rect(0, 12, 244, 40)
            blurSource: fileChildPage
            onTabChange: (index) => {
                fileChildPage.stack(index);
                if (index === 1 && filePage.musicAdapter)
                    filePage.musicAdapter.activateDirectories();
                filePage.setMode = 0;
                filePage.chooseIndex = [];
                filePage.cancelChoose();
            }
        }

        QPages {
            x: 24
            y: 68
            width: fileMain.width - 32
            height: fileMain.height - 68
            id: fileChildPage
            pageList: [myFile,localFile]
            // 我的文件夹
            Item {
                id: myFile
                width: fileChildPage.width
                height: fileChildPage.height
                visible: true

                // 右侧操作区
                Row {
                    x: parent.width - width - 16
                    y: 11
                    z: 2
                    spacing: 8
                    QButton {
                        height: 38
                        text: filePage.setMode === 1 ? "取消选择" : "选择"
                        iconCharacter: "\uf09f"
                        buttonColor: filePage.setMode === 1 ? Style.themes.containColor : Style.themes.fullColor
                        onClicked: {
                            if(filePage.setMode === 1) {
                                filePage.setMode = 0;
                                filePage.chooseIndex = [];
                                filePage.cancelChoose();
                            } else {
                                filePage.setMode = 1;
                            }
                        }
                    }
                    // 添加
                    QButton {
                        height: 38
                        text: "新建文件夹"
                        iconCharacter: "\uf0f8"
                        QAlertDialog {
                            id: dialog
                            title: "新建文件夹"
                            message: "为文件夹设定一个名称："
                            isInput: true
                            //standardButtons: Dialog.Ok | Dialog.Cancel
                            onConfirm: {
                                if(input!=="") {
                                    myFolderModel.addFolder(input, "my", "");
                                    Style.warned("成功添加一个文件夹",1);
                                } else {
                                    Style.warned("请输入文件名",0);
                                }
                            }
                        }
                        onClicked: dialog.open()
                    }
                    QButton {
                        objectName: "legacyCollectionMigrationOpen"
                        height: 38
                        text: "迁移旧歌曲"
                        iconCharacter: "\uf0f1"
                        onClicked: {
                            if (typeof legacyCollectionMigration === "undefined") return;
                            filePage.legacyMigrationPreview = legacyCollectionMigration.preview();
                            if (filePage.legacyMigrationPreview.errorKey) {
                                Style.warned("无法读取旧歌曲数据库，请保留原文件并重试", 0);
                                return;
                            }
                            if (filePage.legacyMigrationPreview.songCount === 0) {
                                Style.warned("我的文件夹中没有待迁移的旧歌曲", 0);
                                return;
                            }
                            filePage.legacyMigrationCandidates = legacyCollectionMigration.candidates();
                            if (filePage.legacyMigrationCandidates.length === 0) {
                                Style.warned("请先在本地音乐插件设置中配置并启用目录", 0);
                                filePage.requestPluginSettings("org.quemusic.source.local", "");
                                return;
                            }
                            legacyMigrationDialog.open();
                        }
                    }
                }

                Dialog {
                    id: legacyMigrationDialog
                    objectName: "legacyCollectionMigrationConfirm"
                    parent: myFile
                    x: (parent.width - width) / 2
                    y: (parent.height - height) / 2
                    width: 420
                    modal: true
                    focus: true
                    title: "确认迁移旧歌曲"
                    standardButtons: Dialog.Ok | Dialog.Cancel
                    onOpened: migrationInstanceChoice.currentIndex = filePage.legacyMigrationCandidates.length === 1 ? 0 : -1
                    onAccepted: {
                        if (migrationInstanceChoice.currentIndex < 0) {
                            Style.warned("请先选择要认领歌曲的本地音乐实例", 0);
                            return;
                        }
                        var result = legacyCollectionMigration.run(migrationInstanceChoice.currentValue);
                        if (!result.committed) {
                            Style.warned("迁移未完成；旧歌曲未被删除，请检查本地插件设置后重试", 0);
                            return;
                        }
                        var pending = result.noMatch + result.ambiguous + result.unavailable + result.invalidPath;
                        Style.warned("已认领 " + result.matched + " 首，待处理 " + pending
                                     + " 首；旧数据库已备份且原记录保留", pending === 0 ? 1 : 0);
                    }
                    contentItem: Column {
                        spacing: 12
                        Text {
                            width: legacyMigrationDialog.availableWidth
                            wrapMode: Text.WordWrap
                            color: Style.themes.fontColor
                            text: "发现 " + filePage.legacyMigrationPreview.songCount
                                  + " 首旧歌曲。请选择本地音乐实例；仅该实例已扫描的文件可被认领。"
                        }
                        ComboBox {
                            id: migrationInstanceChoice
                            objectName: "legacyCollectionMigrationInstance"
                            width: legacyMigrationDialog.availableWidth
                            model: filePage.legacyMigrationCandidates
                            textRole: "displayName"
                            valueRole: "instanceId"
                        }
                        Text {
                            width: legacyMigrationDialog.availableWidth
                            wrapMode: Text.WordWrap
                            color: Style.themes.fontColor
                            text: "确认后先备份旧数据库，再保存逐首认领结果。未认领歌曲不会自动播放或删除，可配置目录并重试。"
                        }
                    }
                }

                QListView {
                    id: folderView
                    anchors.fill: parent
                    model: myFolderModel
                    clip: true
                    topMargin: 60
                    headerModel: ["标题","","","菜单"]
                    function openFilePage(title,image) {
                        folderMusic.opened(title,image)
                        filePage.loaded()
                    }
                    rebound: Transition {
                        NumberAnimation {
                            properties: "y"
                            duration: 480
                            easing.type: Easing.Bezier
                            easing.bezierCurve: [ 0.32, 0.12, 0.00, 1.00, 1, 1 ]
                        }
                    }
                    QAlertDialog {
                        id: editDialog
                        title: "重命名"
                        message: "为文件夹重新命名新名称："
                        isInput: true
                        //property int index
                        property int folderId
                        onConfirm: {
                            if(input!=="") {
                                //myfileModel.setProperty(index, "name", input)
                                //var folderId = myFolderModel.data(myFolderModel.index(folderIndex), 256)
                                myFolderModel.renameFolder(editDialog.folderId, input);
                                mainWarn.tiped("成功修改文件夹名称",1);
                            } else {
                                mainWarn.tiped("请输入文件名",0);
                            }
                        }
                    }
                    delegate: Rectangle {
                        id: listfolder
                        height: 64
                        width: folderView.width - 16
                        radius: Style.settings.labelRadius
                        color: "#00000000"//index % 2 === 0 ? Style.themes.blurOverlayColor : "transparent"
                        Connections {
                            target: filePage
                            function onCancelChoose() {
                                listfolder.color = "#00000000"
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Style.settings.labelRadius
                            color: Style.themes.hoverColor
                            opacity: foldArea.containsMouse ? 1 : 0
                            z: 1
                            Behavior on opacity { NumberAnimation { duration: 80 } }
                        }

                        Rectangle {
                            y: 8
                            x: 8
                            z: 4
                            width: 48
                            height: 48
                            color: Style.themes.containColor
                            radius: 10
                            Text {
                                anchors.fill: parent
                                text: "\uf0f5"
                                font.family: iconFont.name
                                font.pixelSize: Style.settings.texticon
                                color: Style.themes.fontColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }


                        Label {
                            x: 80
                            y: 0
                            z: 3
                            width: 140
                            height: 64
                            text: model.name
                            color: Style.themes.fontColor
                            font.bold: true
                            font.pixelSize: Style.settings.textmain
                            verticalAlignment: Text.AlignVCenter
                            visible: true
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }

                        MouseArea {
                            id: foldArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if(filePage.setMode === 1) {
                                    if(listfolder.color == "#00000000") {
                                        listfolder.color = Style.themes.containColor;
                                        filePage.chooseIndex.push(model.folderId);
                                    } else {
                                        listfolder.color = "#00000000";
                                        filePage.chooseIndex = filePage.chooseIndex.filter(value => value !== model.folderId);
                                    }
                                } else {
                                    filePage.folderNumber = index;
                                    window.exitIndex = 1;
                                    songModel.folderId = model.folderId;
                                    folderView.openFilePage(model.name,"");
                                }
                            }
                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 20
                                spacing: 2
                                z: 2
                                y: 12
                                height: 36
                                SButton {
                                    iconCharacter: "\uf050"
                                    width: 36
                                    height: 36
                                    radius: 18
                                    buttonColor: "transparent"
                                    hoverColor: Qt.rgba(0.5,0.5,0.5,0.2)
                                    shadowEnabled: false
                                    onClicked: {
                                    }
                                }
                                SButton {
                                    iconCharacter: "\uf005"
                                    width: 36
                                    height: 36
                                    radius: 18
                                    buttonColor: "transparent"
                                    hoverColor: Qt.rgba(0.5,0.5,0.5,0.2)
                                    shadowEnabled: false

                                    onClicked: {
                                        if(model.folderId !== 1) {
                                            editDialog.input = model.name;
                                            //editDialog.index = index
                                            editDialog.folderId = model.folderId;
                                            editDialog.open();
                                        } else {
                                            Style.warned("无法修改默认文件夹名称",0);
                                        }
                                    }
                                }
                                SButton {
                                    iconCharacter: "\uf08e"
                                    width: 36
                                    height: 36
                                    radius: 18
                                    buttonColor: "transparent"
                                    hoverColor: Qt.rgba(1.0,0.5,0.5,0.8)
                                    shadowEnabled: false
                                    onClicked: {
                                        if(model.folderId !== 1) {
                                            //myfileModel.remove( index, 1 )
                                            globalDialog.openSimpleDialog("删除", "这将删除本文件夹，无法恢复，是否删除？",
                                                function() {
                                                    myFolderModel.deleteFolder(model.folderId);
                                                    Style.warned("成功删除一个我的文件夹",1);
                                                }
                                            );
                                        } else {
                                            Style.warned("无法删除默认文件夹",0);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 本地文件夹：数据和操作均由 Source Plugin Adapter 提供。
            Item {
                id: localFile
                width: fileChildPage.width
                height: fileChildPage.height
                visible: false

                Text {
                    objectName: "directoryStatus"
                    anchors.centerIn: parent
                    visible: !filePage.musicAdapter || filePage.musicAdapter.directoryItems.count === 0
                    text: {
                        var state = filePage.musicAdapter
                                    ? filePage.musicAdapter.directoryState : "unavailable";
                        var available = filePage.musicAdapter
                                        && filePage.musicAdapter.pluginAvailable("org.quemusic.source.local");
                        return !available || state === "unavailable" ? "本地音乐插件不可用"
                             : state === "loading" ? "正在加载目录…"
                             : state === "failed" ? "目录加载失败，请重试"
                             : "暂无目录，点击导入目录进行配置";
                    }
                    color: Style.themes.fontColor
                    font.pixelSize: Style.settings.textmain
                }

                Row {
                    x: parent.width - width - 16
                    y: 11
                    z: 2
                    spacing: 8
                    QButton {
                        height: 38
                        text: filePage.setMode === 2 ? "取消选择" : "选择"
                        iconCharacter: "\uf09f"
                        buttonColor: filePage.setMode === 2 ? Style.themes.containColor : Style.themes.fullColor
                        onClicked: {
                            filePage.setMode = filePage.setMode === 2 ? 0 : 2;
                            filePage.chooseIndex = [];
                            filePage.cancelChoose();
                        }
                    }
                    QButton {
                        objectName: "importPluginDirectory"
                        height: 38
                        text: "导入目录"
                        iconCharacter: "\uf0f1"
                        onClicked: filePage.requestPluginSettings("org.quemusic.source.local", "")
                    }
                    QButton {
                        objectName: "directoryRetry"
                        height: 38
                        text: "重试"
                        visible: filePage.directoryHasError()
                        enabled: !!filePage.musicAdapter && filePage.musicAdapter.directoryState !== "loading"
                        onClicked: filePage.requestDirectorySections(true)
                    }
                }

                QListView {
                    id: localFolderView
                    objectName: "pluginDirectoryRoots"
                    anchors.fill: parent
                    clip: true
                    topMargin: 60
                    headerModel: ["标题","","","菜单"]
                    model: filePage.musicAdapter ? filePage.musicAdapter.directoryItems : null
                    useLegacyLoadingState: false
                    hasMore: !!model && model.hasMore
                    loadingMore: !!model && model.loadingMore && model.paginationSectionIds.length === 0
                    sectionError: filePage.directoryHasError() ? ({failed: true}) : ({})
                    retryAction: function() { filePage.requestDirectorySections(true) }
                    isEnd: count > 0 && !hasMore && !loadingMore && !filePage.directoryHasError()
                    visible: !filePage.musicAdapter || !filePage.musicAdapter.directoryCanNavigateBack
                    function activateRow(rowIndex) {
                        if (!filePage.musicAdapter) return;
                        var row = filePage.directoryRow(localFolderView, rowIndex);
                        if (!row || row.isError || row.entityType !== 5) return;
                        if (filePage.setMode === 2) {
                            var selected = filePage.chooseIndex.indexOf(rowIndex);
                            if (selected < 0) filePage.chooseIndex.push(rowIndex);
                            else filePage.chooseIndex.splice(selected, 1);
                            filePage.chooseIndexChanged();
                            return;
                        }
                        if (filePage.directoryCapabilities(row).canBrowse && filePage.musicAdapter.browseDirectory(row)) {
                            window.exitIndex = 1;
                            localFolderMusic.opened(row.title, "");
                        }
                    }
                    function manageRow(rowIndex) {
                        if (!filePage.musicAdapter) return;
                        var row = filePage.directoryRow(localFolderView, rowIndex);
                        if (row && row.settingsPackageId)
                            filePage.requestPluginSettings(row.settingsPackageId, row.settingsInstanceId || "");
                    }
                    function loadMoreVisibleSections() { filePage.requestDirectorySections(false); }
                    onAtYEndChanged: if (visible && atYEnd) loadMoreVisibleSections()
                    Connections {
                        target: filePage.musicAdapter
                        function onDirectoryChanged() {
                            if (localFolderView.visible && localFolderView.atYEnd)
                                Qt.callLater(localFolderView.loadMoreVisibleSections);
                        }
                    }
                    delegate: Rectangle {
                        height: 64
                        width: localFolderView.width - 16
                        radius: Style.settings.labelRadius
                        color: filePage.chooseIndex.indexOf(index) >= 0
                               ? Style.themes.containColor : "transparent"
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: Style.themes.hoverColor
                            opacity: rootArea.containsMouse ? 1 : 0
                        }
                        Text {
                            x: 20; width: 44; height: parent.height
                            text: "\uf0f5"
                            font.family: iconFont.name
                            color: Style.themes.fontColor
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 80; width: parent.width - 185; height: parent.height
                            text: model.title
                            color: Style.themes.fontColor
                            font.pixelSize: Style.settings.textmain
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: rootArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: localFolderView.activateRow(index)
                        }
                        SButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.verticalCenter: parent.verticalCenter
                            width: 36; height: 36
                            iconCharacter: "\\uf005"
                            visible: !!model.settingsPackageId
                            onClicked: localFolderView.manageRow(index)
                        }
                    }
                }
            }
        }
    }

    AnimatorWindow {
        id: folderMusic
        mainTarget: fileMain
        winIndex: 1

        content: Item {
            anchors.fill: parent
            Connections {
                target: filePage
                function onLoaded() {
                    console.log("更新音乐文件夹列表成功:",folderMusic.musicList);
                }
            }

            FileDialog {
                id: musicfileDialog
                title: "选择音乐文件"
                fileMode: FileDialog.OpenFiles
                nameFilters: ["音频文件 (*.mp3 *.wav *.aac *.flac *.ogg *.eac3 *.wma *.ac3 *.alac *.mkv *.wmv *.avi *.mpeg4)"]
                onAccepted: {
                    // 获取选中的文件URL（file:// 格式）
                    var fileUrls = musicfileDialog.selectedFiles;

                    // 将URL转换为本地文件路径（去掉 'file:///' 前缀）
                    for (var i = 0; i < fileUrls.length; i++) {
                        var fileUrl = fileUrls[i];
                        var filePath = fileUrl.toString();
                        if (filePath.startsWith("file:///")) {
                            filePath = filePath.substring(8);// 去前8字符：file:///
                        }

                        // 从完整路径中提取纯文件名（例如从 'C:/Users/me/doc.txt' 提取 'doc.txt'）
                        var fileName = filePath.split('/').pop(); // 使用 '/' 分割，取最后一部分

                        console.log("文件URL: ", fileUrl);
                        console.log("文件路径: ", filePath);
                        console.log("文件名: ", fileName);

                        // 现在你可以使用 fileName 或 filePath 进行后续操作，例如显示、读取等
                        let musics = [];
                        //musics.push({name:fileName,path:filePath,songer:""});
                        //myfileModel.get(filePage.folderNumber).music.append(musics);
                        songModel.addSong(songModel.folderId, fileName, filePath, "");
                        Style.warned("成功导入音乐",1);
                        //filePage.loaded()
                    }
                }
                onRejected: {
                    console.log("操作取消");
                }
            }

            // 顶栏
            Row {
                x: 144
                y: 76
                height: 36
                spacing: 6
                QButton {
                    height: 36; width: 96
                    radius: Style.settings.labelRadius
                    iconCharacter: "\uf00e"
                    text: "播放"
                    shadowEnabled: false
                    buttonColor: Style.themes.sideColor
                    onClicked: {
                    }
                }
                SButton {
                    width: 36
                    height: 36
                    radius: Style.settings.labelRadius
                    iconCharacter: "\uf095"
                    shadowEnabled: false
                    buttonColor: Style.themes.sideColor
                    onClicked: {
                    }
                }
                SButton {
                    width: 36
                    height: 36
                    radius: Style.settings.labelRadius
                    iconCharacter: "\uf0c8"
                    shadowEnabled: false
                    buttonColor: Style.themes.sideColor
                    onClicked: {
                    }
                }
            }

            QButton {
                x: localFolderMusic.width - 124
                y: 44
                height: 40; width: 100
                radius: 20
                z: 10
                iconCharacter: "\uf10d"
                text: "导入"
                onClicked: {
                    musicfileDialog.open();
                }
            }

            QListView {
                id: fileView
                x: 24
                y: 128
                width: folderMusic.width - 32
                height: folderMusic.height - 128
                model: songModel//parent.visible ? folderMusic.foldercontent : []
                clip: true
                //reuseItems: true
                headerModel: ["标题","","","菜单"]
                delegate: Rectangle {
                    id: listfile
                    property string migrationStatus: filePage.legacySongStatus(songModel.folderId, model.songId)
                    height: 60
                    width: fileView.width - 16
                    radius: Style.settings.labelRadius
                    color: mainMedia.noTitle == model.name ? Style.themes.containColor : "transparent"

                    Behavior on color { ColorAnimation { duration: 120 } }

                    Rectangle {
                        y: 8
                        x: 8
                        z: 4
                        width: 44
                        height: 44
                        color: Style.themes.containColor
                        radius: 10
                        Text {
                            anchors.fill: parent
                            text: "\uf044"
                            font.family: iconFont.name
                            font.pixelSize: Style.settings.texticon
                            color: Style.themes.fontColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: Style.settings.labelRadius
                        color: Style.themes.hoverColor
                        opacity: fileArea.containsMouse ? 1 : 0
                        z: 1
                        Behavior on opacity { NumberAnimation { duration: 80 } }
                    }


                    Label {
                        x: 80
                        y: 0
                        z: 3
                        width: 140
                        height: 60
                        text: model.name
                        color: Style.themes.fontColor
                        font.bold: true
                        font.pixelSize: Style.settings.textmain
                        verticalAlignment: Text.AlignVCenter
                        visible: true
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }

                    Text {
                        x: 80
                        y: 38
                        z: 3
                        color: Style.themes.fontColor
                        font.pixelSize: 11
                        text: listfile.migrationStatus === "matched" ? "已认领 · 插件播放"
                            : listfile.migrationStatus === "noMatch" ? "未匹配：请配置目录并重试"
                            : listfile.migrationStatus === "ambiguous" ? "多个实例匹配：请重新选择"
                            : listfile.migrationStatus === "invalidPath" ? "旧路径无效"
                            : listfile.migrationStatus === "stale" ? "旧记录已变化：请重试迁移"
                            : listfile.migrationStatus === "notMigrated" ? "待迁移：请先认领"
                            : "暂不可用：请重试迁移"
                    }

                    MouseArea {
                        id: fileArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (filePage.legacySongBlocked(listfile.migrationStatus)) {
                                Style.warned("这首旧歌曲尚未认领，不能直接播放；请配置本地插件并重试迁移", 0);
                                return;
                            }
                            if (!legacyCollectionMigration.playSong(songModel.folderId, model.songId))
                                Style.warned("插件暂无法播放这首歌，请检查目录后重试", 0);
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            spacing: 2
                            y: 12
                            height: 36
                            SButton {
                                id: fileListAdd
                                iconCharacter: "\uf095"
                                width: 36
                                height: 36
                                radius: 18
                                buttonColor: "transparent"
                                hoverColor: Qt.rgba(0.5,0.5,0.5,0.2)
                                shadowEnabled: false
                                onClicked: {
                                    if (filePage.legacySongBlocked(listfile.migrationStatus)) {
                                        Style.warned("这首旧歌曲尚未认领，不能加入播放列表", 0);
                                        return;
                                    }
                                    if (legacyCollectionMigration.enqueueSong(songModel.folderId, model.songId))
                                        Style.warned("成功加入播放列表", 1);
                                    else
                                        Style.warned("插件暂无法加入这首歌，请检查目录后重试", 0);
                                }
                            }
                            SButton {
                                id: fileOpen
                                iconCharacter: "\uf107"
                                width: 36
                                height: 36
                                radius: 18
                                buttonColor: "transparent"
                                hoverColor: Qt.rgba(0.5,0.5,0.5,0.2)
                                shadowEnabled: false
                                onClicked: {
                                }
                            }
                            SButton {
                                id: fileDelete
                                iconCharacter: "\uf08e"
                                width: 36
                                height: 36
                                radius: 18
                                buttonColor: "transparent"
                                hoverColor: Qt.rgba(1.0,0.5,0.5,0.8)
                                shadowEnabled: false
                                onClicked: {
                                    //myfileModel.get(filePage.folderNumber).music.remove(index)
                                    songModel.deleteSong(model.songId);
                                    Style.warned("成功删除一个音乐",1);
                                }
                            }
                        }
                    }

                    function findIndexByValue(model, key, targetValue) {
                        for (var i = 0; i < model.count; i++) {
                            var element = model.get(i);
                            if (element[key] === targetValue) {
                                return i; // 返回找到的索引
                            }
                        }
                        return -1; // 未找到返回 -1
                    }
                }
            }
        }
    }

    AnimatorWindow {
        id: localFolderMusic
        mainTarget: fileMain
        winIndex: 1
        content: Item {
            anchors.fill: parent
            Row {
                x: 144
                y: 76
                height: 36
                spacing: 6
                QButton {
                    objectName: "pluginDirectoryBack"
                    text: "返回"
                    iconCharacter: "\uf053"
                    height: 36
                    onClicked: {
                        if (!filePage.musicAdapter || !filePage.musicAdapter.directoryBack()) return;
                        if (!filePage.musicAdapter.directoryCanNavigateBack)
                            localFolderMusic.closed();
                    }
                }
                QButton {
                    text: "刷新"
                    height: 36
                    onClicked: if (filePage.musicAdapter) filePage.musicAdapter.refreshDirectories()
                }
            }
            QButton {
                x: localFolderMusic.width - width - 24
                y: 44
                height: 40
                text: "文件位置由插件管理"
                enabled: false
                ToolTip.visible: hovered
                ToolTip.text: "目录身份不是文件打开权限，请在插件设置中管理目录"
            }
            Text {
                anchors.centerIn: parent
                visible: filePage.musicAdapter && filePage.musicAdapter.directoryItems.count === 0
                text: filePage.musicAdapter && filePage.musicAdapter.directoryState === "failed"
                      ? "目录加载失败，请重试" : "目录中没有歌曲"
                color: Style.themes.fontColor
                font.pixelSize: Style.settings.textmain
            }
            QListView {
                id: localFileView
                objectName: "pluginDirectoryContents"
                x: 24
                y: 128
                width: localFolderMusic.width - 32
                height: localFolderMusic.height - 128
                model: filePage.musicAdapter ? filePage.musicAdapter.directoryItems : null
                useLegacyLoadingState: false
                hasMore: !!model && model.hasMore
                loadingMore: !!model && model.loadingMore && model.paginationSectionIds.length === 0
                sectionError: filePage.directoryHasError() ? ({failed: true}) : ({})
                retryAction: function() { filePage.requestDirectorySections(true) }
                isEnd: count > 0 && !hasMore && !loadingMore && !filePage.directoryHasError()
                clip: true
                headerModel: ["标题","","","菜单"]
                function activateRow(rowIndex) {
                    if (!filePage.musicAdapter) return;
                    var row = filePage.directoryRow(localFileView, rowIndex);
                    if (!row) return;
                    var caps = filePage.directoryCapabilities(row);
                    if (row.entityType === 5 && caps.canBrowse)
                        filePage.musicAdapter.browseDirectory(row);
                    else if (row.entityType === 0 && caps.canPlay)
                        filePage.musicAdapter.play(row);
                }
                function enqueueRow(rowIndex) {
                    var row = filePage.directoryRow(localFileView, rowIndex);
                    if (row && row.entityType === 0 && filePage.directoryCapabilities(row).canEnqueue)
                        filePage.musicAdapter.enqueue(row);
                }
                onAtYEndChanged: if (visible && atYEnd) filePage.requestDirectorySections(false)
                Connections {
                    target: filePage.musicAdapter
                    function onDirectoryChanged() {
                        if (localFileView.visible && localFileView.atYEnd)
                            Qt.callLater(filePage.requestDirectorySections, false);
                    }
                }
                delegate: Rectangle {
                    height: 60
                    width: localFileView.width - 16
                    radius: Style.settings.labelRadius
                    color: "transparent"
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Style.themes.hoverColor
                        opacity: contentArea.containsMouse ? 1 : 0
                    }
                    Text {
                        x: 20; width: 44; height: parent.height
                        text: model.entityType === 5 ? "\uf0f5" : "\uf044"
                        font.family: iconFont.name
                        color: Style.themes.fontColor
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        x: 80; width: parent.width - 185; height: parent.height
                        text: model.title
                        color: Style.themes.fontColor
                        font.pixelSize: Style.settings.textmain
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: contentArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: localFileView.activateRow(index)
                    }
                    SButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36; height: 36
                        iconCharacter: "\uf095"
                        visible: model.entityType === 0 && filePage.directoryCapabilities(filePage.directoryRow(localFileView, index)).canEnqueue
                        onClicked: localFileView.enqueueRow(index)
                    }
                }
            }
        }
    }
    // 选择模式
    Rectangle {
        id: chooseArea
        x: 0
        y: visible ? filePage.height - 60 : filePage.height
        width: filePage.width
        height: 60
        visible: filePage.setMode !== 0
        color: Style.themes.sideColor
        Behavior on y { NumberAnimation { duration: 420; easing.type: Easing.OutExpo } }
        Rectangle {
            x: 16
            y: 12
            width: 92
            height: 36
            radius: 20
            color: Style.themes.fullColor
            Text {
                anchors.centerIn: parent
                text: "多选模式"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.textmain
            }
        }
        Rectangle {
            x: 118
            y: 12
            width: 92
            height: 36
            radius: 20
            color: "transparent"//Style.themes.fullColor
            Text {
                anchors.centerIn: parent
                text: "已选择:" + filePage.chooseIndex.length + "项"
                color: Style.themes.textColor
                font.pixelSize: Style.settings.textmain
            }
        }
        QButton {
            y: 12
            x: chooseArea.width - 208
            shadowEnabled: false
            width: 92
            height: 36
            radius: 20
            buttonColor: "#fa4642"
            textColor: Style.themes.primaryColor
            text: filePage.setMode === 2 ? "管理" : "删除"
            onClicked: {
                switch(filePage.setMode) {
                case 1:
                    globalDialog.openSimpleDialog("删除", "这将删除这些文件夹，无法恢复，是否删除？",
                        function() {
                            for(var i=0;i<filePage.chooseIndex.length;i++) {
                                myFolderModel.deleteFolder(filePage.chooseIndex[i]);
                            }
                            filePage.chooseIndex = [];
                            Style.warned("成功删除" + filePage.chooseIndex.length + "个我的文件夹",1);
                        }
                    );
                    break;
                case 2:
                    if (filePage.chooseIndex.length > 0)
                        localFolderView.manageRow(filePage.chooseIndex[0]);
                    break;
                default:
                    break;
                }
            }
        }
        QButton {
            y: 12
            x: chooseArea.width - 108
            shadowEnabled: false
            width: 92
            height: 36
            radius: 20
            buttonColor: Style.themes.themeColor
            textColor: Style.themes.primaryColor
            text: "完成"
            onClicked: {
                filePage.setMode = 0;
                filePage.chooseIndex = [];
                filePage.cancelChoose()
            }
        }
    }
}
