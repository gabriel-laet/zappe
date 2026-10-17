import QtQuick

Item {
    anchors.fill: parent
    visible: opacity > 0.01
    opacity: guide.detailOpen ? 1 : 0
    Behavior on opacity {
        NumberAnimation {
            duration: 280
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#8A000000"
    }

    Column {
        x: 88
        y: 168
        spacing: 16
        width: 900

        Text {
            visible: guide.focusService.length > 0
            text: guide.focusService.toUpperCase()
            color: guide.accent
            font.family: guide.uiFont
            font.pixelSize: 18
            font.weight: 600
            font.letterSpacing: 2.4
        }
        Text {
            width: parent.width
            text: guide.focusTitle
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 72
            font.weight: 600
            font.letterSpacing: -1.5
            wrapMode: Text.WordWrap
        }
        Text {
            visible: guide.focusShelf.length > 0
                     && guide.focusShelf.toLowerCase() !== guide.focusService.toLowerCase()
            text: guide.focusShelf
            color: "#aeaeb2"
            font.family: guide.uiFont
            font.pixelSize: 26
        }
        Text {
            width: 680
            topPadding: 6
            text: "In your library. This guide only browses the local catalog."
            color: "#d1d1d6"
            font.family: guide.uiFont
            font.pixelSize: 24
            wrapMode: Text.WordWrap
        }
        Rectangle {
            visible: guide.focusProgress > 0.01
            width: 380
            height: 6
            radius: 3
            color: "#33ffffff"
            Rectangle {
                width: parent.width * guide.focusProgress
                height: parent.height
                radius: 3
                color: "#f5f5f7"
            }
        }
        Rectangle {
            id: pill
            width: 196
            height: 64
            radius: 32
            color: guide.accent
            scale: 1
            Text {
                anchors.centerIn: parent
                text: "Play"
                color: guide.accentInk
                font.family: guide.uiFont
                font.pixelSize: 26
                font.weight: 600
            }
        }
        Text {
            visible: guide.focusHref.length > 0
            text: guide.focusHref
            color: "#6e6e73"
            font.family: guide.monoFont
            font.pixelSize: 16
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 88
        anchors.bottomMargin: 48
        text: "Esc     Back"
        color: "#8e8e93"
        font.family: guide.uiFont
        font.pixelSize: 22
    }

    Connections {
        target: guide
        function onActivated() {
            pulse.restart()
        }
    }
    SequentialAnimation {
        id: pulse
        NumberAnimation {
            target: pill
            property: "scale"
            to: 0.94
            duration: 80
        }
        NumberAnimation {
            target: pill
            property: "scale"
            to: 1
            duration: 220
            easing.type: Easing.OutBack
        }
    }
}
