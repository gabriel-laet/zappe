import QtQuick

Item {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 64
    width: 460
    height: 76
    z: 12
    opacity: guide.volumeHud ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 160 } }

    Rectangle {
        anchors.fill: parent
        radius: 38
        color: "#EE141418"
        border.color: "#33ffffff"
        border.width: 1

        Text {
            id: word
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            text: guide.muted ? "Mudo" : "Volume"
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 22
            font.weight: 600
            width: 96
        }
        Rectangle {
            anchors.left: word.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 230
            height: 8
            radius: 4
            color: "#33ffffff"
            Rectangle {
                width: parent.width * ((guide.muted || guide.volume <= 0) ? 0 : guide.volume / 100)
                height: parent.height
                radius: 4
                color: guide.accent
                Behavior on width { NumberAnimation { duration: 120 } }
            }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            text: guide.muted ? "0" : guide.volume
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 22
            font.weight: 600
            horizontalAlignment: Text.AlignRight
            width: 48
        }
    }
}
