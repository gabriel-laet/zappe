import QtQuick

Item {
    id: menu
    width: 440
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    z: 6
    x: guide.menuOpen ? 0 : -width
    visible: x > -width + 2
    property bool motion: false

    Behavior on x {
        enabled: menu.motion
        NumberAnimation { duration: Motion.screenMs; easing.type: Motion.ease }
    }

    readonly property var labels: ["Início", "Ao vivo", "Streaming", "Ajustes"]
    readonly property var hints: ["Todas as fileiras", "Canais no ar", "O que está no catálogo", "Imagem e marca"]

    Rectangle {
        anchors.fill: parent
        color: "#C20C0C10"
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: "#22ffffff"
    }

    Text {
        x: 40
        y: 48
        text: guide.brandName
        color: "#f5f5f7"
        font.family: guide.uiFont
        font.pixelSize: 28
        font.weight: 600
        width: 340
        elide: Text.ElideRight
    }

    Rectangle {
        x: 28
        width: 384
        height: 84
        radius: 20
        color: "#28ffffff"
        y: 128 + guide.menuIndex * 96
        Behavior on y {
            enabled: menu.motion
            NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
        }
    }

    Column {
        x: 28
        y: 128
        spacing: 12
        Repeater {
            model: 4
            Item {
                required property int index
                width: 384
                height: 84
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: guide.menuIndex === index ? 28 : 0
                    radius: 2
                    color: guide.accent
                    Behavior on height { NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease } }
                }
                Column {
                    x: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: menu.labels[index]
                        color: guide.menuIndex === index ? "#f5f5f7" : "#8e8e93"
                        font.family: guide.uiFont
                        font.pixelSize: 28
                        font.weight: 600
                    }
                    Text {
                        text: menu.hints[index]
                        color: "#6e6e73"
                        font.family: guide.uiFont
                        font.pixelSize: 16
                    }
                }
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 40
        anchors.bottomMargin: 40
        text: "Esc fecha"
        color: "#6e6e73"
        font.family: guide.uiFont
        font.pixelSize: 18
    }

    Component.onCompleted: menu.motion = true
}
