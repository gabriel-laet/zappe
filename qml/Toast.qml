import QtQuick

Item {
    id: toast
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: guide.volumeHud ? 156 : (guide.detailOpen ? 300 : 72)
    height: 68
    z: 13
    opacity: guide.toastOn ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Behavior on anchors.bottomMargin { NumberAnimation { duration: 180 } }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(920, label.implicitWidth + 64)
        height: 68
        radius: 34
        color: "#F2F5F5F7"

        Text {
            id: label
            anchors.centerIn: parent
            text: guide.toastText
            color: "#141416"
            font.family: guide.uiFont
            font.pixelSize: 24
            font.weight: 600
            elide: Text.ElideRight
            width: Math.min(860, implicitWidth)
        }
    }
}
