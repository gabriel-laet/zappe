import QtQuick

Item {
    Rectangle {
        anchors.fill: parent
        color: guide.background
    }

    Column {
        anchors.centerIn: parent
        spacing: 22

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: guide.splashUrl
            visible: guide.splashUrl.length > 0
            width: 200
            height: 200
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: guide.brandName
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: guide.splashUrl.length > 0 ? 42 : 84
            font.weight: 600
            font.letterSpacing: -1.2
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56
            height: 4
            radius: 2
            color: guide.accent
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: guide.tagline.length > 0
            text: guide.tagline
            color: "#aeaeb2"
            font.family: guide.uiFont
            font.pixelSize: 22
        }
    }
}
