import QtQuick

Rectangle {
    visible: guide.showFps && guide.fps > 0
    anchors.left: parent.left
    anchors.bottom: parent.bottom
    anchors.margins: 28
    radius: 10
    color: "#AA000000"
    width: fpsText.implicitWidth + 22
    height: fpsText.implicitHeight + 12

    Text {
        id: fpsText
        anchors.centerIn: parent
        text: guide.fps + " fps"
        color: "#f5f5f7"
        font.family: guide.monoFont
        font.pixelSize: 16
    }
}
