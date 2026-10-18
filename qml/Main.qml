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
        z: 0
    }

    Rectangle {
        anchors.fill: parent
        z: 1
        color: "#000000"
        opacity: guide.menuOpen ? 0.28 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease } }
    }

    Item {
        id: stage
        z: 2
        width: parent.width
        height: parent.height
        x: guide.menuOpen ? 300 : 0
        scale: guide.menuOpen ? 0.94 : 1
        opacity: guide.menuOpen ? 0.92 : 1
        transformOrigin: Item.Left
        property bool motion: false

        Behavior on x {
            enabled: stage.motion
            NumberAnimation { duration: Motion.screenMs; easing.type: Motion.ease }
        }
        Behavior on scale {
            enabled: stage.motion
            NumberAnimation { duration: Motion.screenMs; easing.type: Motion.ease }
        }
        Behavior on opacity { NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease } }

        HomeView {
            anchors.fill: parent
            opacity: win.splashOn ? 0 : 1
            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.screenMs
                    easing.type: Motion.ease
                }
            }
        }

        DetailView {
            anchors.fill: parent
            z: 3
        }

        SettingsView {
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
            Behavior on opacity { NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease } }

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

        Component.onCompleted: stage.motion = true
    }

    SideMenu {}

    VolumeHud {}
    Toast {}

    SplashView {
        anchors.fill: parent
        z: 6
        opacity: win.splashOn ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: Motion.screenMs
                easing.type: Motion.ease
            }
        }
    }

    FpsOverlay {
        z: 8
    }

    LoadingView {
        z: 13
    }

    LoginPrompt {
        z: 15
    }

    Rectangle {
        z: 11
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 72
        visible: house.listening
        radius: 18
        color: "#141416"
        border.color: guide.accent
        border.width: 2
        width: listenLabel.implicitWidth + 48
        height: 64
        Text {
            id: listenLabel
            anchors.centerIn: parent
            text: house.listeningText.length > 0 ? house.listeningText : "Ouvindo…"
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 24
        }
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        z: 20
        Keys.onPressed: function (event) {
            const backKey = event.key === Qt.Key_Escape || event.key === Qt.Key_Back
                    || event.key === Qt.Key_Backspace || event.key === Qt.Key_Home
                    || event.key === Qt.Key_HomePage
            if (backKey && event.isAutoRepeat) {
                event.accepted = true
                return
            }
            if (win.splashOn) {
                win.splashOn = false
                event.accepted = true
                return
            }
            if (house.promptOpen) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Home
                        || event.key === Qt.Key_HomePage)
                    house.cancelPrompt()
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    house.submitPrompt(house.draft)
                event.accepted = true
                return
            }
            if (house.loading) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace
                        || event.key === Qt.Key_Home || event.key === Qt.Key_HomePage)
                    house.cancelLoading()
                else if (house.loadingFailed && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        || event.key === Qt.Key_Select))
                    house.retryLoading()
                else if (event.key === Qt.Key_VolumeUp)
                    guide.adjustVolume(5)
                else if (event.key === Qt.Key_VolumeDown)
                    guide.adjustVolume(-5)
                else if (event.key === Qt.Key_VolumeMute)
                    guide.toggleMute()
                event.accepted = true
                return
            }
            if (house.playing && (event.key === Qt.Key_Escape || event.key === Qt.Key_Back
                    || event.key === Qt.Key_Backspace || event.key === Qt.Key_Home || event.key === Qt.Key_HomePage)) {
                house.goBack()
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
            case Qt.Key_Home:
            case Qt.Key_HomePage:
                house.goBack()
                break
            case Qt.Key_Menu:
            case Qt.Key_Multi_key:
                guide.toggleMenu()
                break
            case Qt.Key_VolumeUp:
                guide.adjustVolume(5)
                break
            case Qt.Key_VolumeDown:
                guide.adjustVolume(-5)
                break
            case Qt.Key_VolumeMute:
                guide.toggleMute()
                break
            case Qt.Key_PageUp:
                guide.move(-1, 0)
                break
            case Qt.Key_PageDown:
                guide.move(1, 0)
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
