import QtQuick

Window {
    id: win
    width: 1920
    height: 1080
    visible: true
    title: guide.brandName
    color: guide.background
    x: 0
    y: 0
    visibility: guide.windowed ? Window.Windowed : Window.FullScreen
    flags: guide.frameless ? Qt.FramelessWindowHint : Qt.Window

    property bool splashOn: !guide.skipSplash

    Backdrop {
        anchors.fill: parent
    }

    HomeView {
        anchors.fill: parent
        opacity: win.splashOn ? 0 : 1
        Behavior on opacity {
            NumberAnimation {
                duration: 420
                easing.type: Easing.OutCubic
            }
        }
    }

    DetailView {
        anchors.fill: parent
        z: 3
    }

    Item {
        id: header
        z: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 80
        anchors.rightMargin: 80
        anchors.topMargin: 36
        height: 64
        opacity: win.splashOn ? 0 : 1
        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            Image {
                width: 52
                height: 52
                source: guide.logoUrl
                visible: guide.logoUrl.length > 0
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: guide.brandName
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 30
                    font.weight: 600
                    font.letterSpacing: -0.4
                    width: Math.min(implicitWidth, 560)
                    elide: Text.ElideRight
                }
                Text {
                    visible: guide.tagline.length > 0
                    text: guide.tagline
                    color: "#8e8e93"
                    font.family: guide.uiFont
                    font.pixelSize: 16
                    width: 560
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            id: clock
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: Qt.formatTime(now, "HH:mm")
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 32
            font.weight: 500

            property date now: new Date()
            Timer {
                interval: 1000
                running: true
                repeat: true
                onTriggered: clock.now = new Date()
            }
        }
    }

    SplashView {
        anchors.fill: parent
        z: 6
        opacity: win.splashOn ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: 420
                easing.type: Easing.OutCubic
            }
        }
    }

    FpsOverlay {
        z: 8
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        z: 7
        Keys.onPressed: function (event) {
            if (win.splashOn) {
                win.splashOn = false
                event.accepted = true
                return
            }
            switch (event.key) {
            case Qt.Key_Left:
                guide.move(0, -1)
                break
            case Qt.Key_Right:
                guide.move(0, 1)
                break
            case Qt.Key_Up:
                guide.move(-1, 0)
                break
            case Qt.Key_Down:
                guide.move(1, 0)
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Select:
                guide.activate()
                break
            case Qt.Key_Escape:
            case Qt.Key_Back:
            case Qt.Key_Backspace:
                guide.back()
                break
            default:
                event.accepted = false
                return
            }
            event.accepted = true
        }
    }

    Timer {
        interval: 1300
        running: win.splashOn
        onTriggered: win.splashOn = false
    }

    Component.onCompleted: keys.forceActiveFocus()
    onActiveChanged: {
        if (active)
            keys.forceActiveFocus()
    }
}
