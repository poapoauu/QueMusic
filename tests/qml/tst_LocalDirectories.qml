import QtQuick
import "qrc:/QueMusic/pages" as Pages

Item {
    property var directoryAdapter
    width: 900
    height: 600
    Pages.FilePage {
        objectName: "filePageUnderTest"
        anchors.fill: parent
        musicAdapter: directoryAdapter
    }
}
