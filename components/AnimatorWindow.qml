// AnimatorWindow.qml
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 QueMusic Contributors
//
import QtQuick
import QueMusic 1.0

Item {
    id: root
    //color: Style.themes.primaryColor
    z: 20
    //border.color: Style.themes.secondaryColor
    visible: false
    property string image: ""
    anchors.fill: parent
    property int winIndex: 1
    property string title: "MusicFolder"
    property var mainTarget
    property bool haveControl: true

    default property alias content: loadWidget.sourceComponent
    function opened(title,image) {
        windowCloseAnime.stop();
        windowOpenAnime.stop();
        root.title = title;
        root.image = image;
        root.visible = true;
        if (loadWidget.active && loadWidget.status === Loader.Ready)
            windowOpenAnime.start();
        else
            loadWidget.active = true;
    }
    function closed(title,image) {
        windowOpenAnime.running = false;
        mainTarget.visible = true;
        windowCloseAnime.running = true;
        window.exitIndex -= 1;
    }
    // Host-private immediate invalidation. A Source context change must not
    // leave a closing animation or lazy content attached to the next scope.
    function resetView() {
        const wasOpen = root.visible || loadWidget.active;
        windowOpenAnime.stop();
        windowCloseAnime.stop();
        loadWidget.active = false;
        root.visible = false;
        root.title = "";
        root.image = "";
        root.scale = 1;
        root.opacity = 1;
        if (root.mainTarget) {
            root.mainTarget.visible = true;
            root.mainTarget.scale = 1;
            root.mainTarget.opacity = 1;
        }
        if (wasOpen && window.exitIndex === root.winIndex)
            window.exitIndex = Math.max(0, root.winIndex - 1);
    }
    Connections {
        target: window
        enabled: root.visible
        function onExit() {
            if(window.exitIndex <= root.winIndex) {
                windowOpenAnime.running = false;
                root.mainTarget.visible = true;
                windowCloseAnime.running = true;
            }
        }
    }

    SequentialAnimation {
        id: windowOpenAnime
    ParallelAnimation {
        NumberAnimation {
            target: root
            property: "scale"
            from: 0.8
            to: 1
            easing.type: Easing.OutExpo
            duration: 360
        }
        NumberAnimation {
            target: root
            property: "opacity"
            from: 0
            to: 1
            easing.type: Easing.OutExpo
            duration: 360
        }
        NumberAnimation {
            target: mainTarget
            property: "scale"
            from: 1
            to: 1.1
            duration: 100
        }
        NumberAnimation {
            target: mainTarget
            property: "opacity"
            from: 1
            to: 0
            duration: 100
        }
    }
    ScriptAction {
        script: root.mainTarget.visible = false
    }
    }
    SequentialAnimation {
        id: windowCloseAnime
    ParallelAnimation {
        NumberAnimation {
            target: root
            property: "scale"
            from: 1
            to: 0.9
            duration: 100
        }
        NumberAnimation {
            target: root
            property: "opacity"
            from: 1
            to: 0
            duration: 100
        }
        NumberAnimation {
            target: mainTarget
            property: "scale"
            from: 1.2
            to: 1
            easing.type: Easing.OutExpo
            duration: 280
        }
        NumberAnimation {
            target: mainTarget
            property: "opacity"
            from: 0
            to: 1
            easing.type: Easing.OutExpo
            duration: 280
        }
    }
    ScriptAction {
        script: {
            loadWidget.active = false
            root.visible = false
        }
    }
    }

    QPicture {
        id: headPic
        y: 16
        x: 16
        z: 4
        width: 96
        height: 96
        radius: 16
        source: root.image || "qrc:/QueMusic/resources/app/musicpic.png"
    }

    Text {
        id: headTitle
        objectName: "animatorWindowTitle"
        x: 144
        y: root.haveControl ? 16 : 34
        width: 200
        height: 60
        color: Style.themes.fontColor
        text: root.title
        textFormat: Text.PlainText
        font.pixelSize: Style.settings.textH1
        verticalAlignment: Text.AlignVCenter
    }

    Loader {
        id: loadWidget
        active: false
        onLoaded: {
            root.visible = true
            windowOpenAnime.start()
        }
    }
}
