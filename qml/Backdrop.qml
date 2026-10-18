import QtQuick

Item {
    Rectangle {
        anchors.fill: parent
        color: guide.background
    }

    Rectangle {
        anchors.fill: parent
        color: guide.focusColor
        opacity: 0.58
        Behavior on color {
            ColorAnimation {
                duration: Motion.screenMs
                easing.type: Motion.ease
            }
        }
    }

    Item {
        id: plates
        anchors.fill: parent
        property bool showA: true
        property bool primed: false
        property string shown: ""

        function show(src) {
            if (primed && src === shown)
                return
            shown = src
            if (!primed) {
                plateA.source = src
                showA = true
                primed = true
                return
            }
            if (showA) {
                plateB.source = src
                showA = false
            } else {
                plateA.source = src
                showA = true
            }
        }

        Image {
            id: plateA
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            cache: true
            opacity: plates.showA ? 0.84 : 0
            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.screenMs
                    easing.type: Motion.ease
                }
            }
        }
        Image {
            id: plateB
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            cache: true
            opacity: plates.showA ? 0 : 0.84
            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.screenMs
                    easing.type: Motion.ease
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#F2000000" }
            GradientStop { position: 0.26; color: "#66000000" }
            GradientStop { position: 0.52; color: "#33000000" }
            GradientStop { position: 1.0; color: "#F7000000" }
        }
    }

    Item {
        anchors.centerIn: parent
        width: parent.height
        height: parent.width
        rotation: -90
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#C0000000" }
                GradientStop { position: 0.46; color: "#00000000" }
            }
        }
    }

    Component.onCompleted: plates.show(guide.focusAmbient)
    Connections {
        target: guide
        function onFocusChanged() {
            plates.show(guide.focusAmbient)
        }
    }
}
