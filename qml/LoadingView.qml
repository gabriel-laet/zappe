import QtQuick

Item {
    anchors.fill: parent
    visible: house.loading
    z: 13

    Rectangle {
        anchors.fill: parent
        color: "#F0000000"
    }

    Column {
        anchors.centerIn: parent
        width: 720
        spacing: 22

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 420
            height: 236
            radius: 28
            color: "#141416"
            border.color: house.loadingFailed ? "#E23B2F" : "#2c2c30"
            border.width: 2
            clip: true

            Image {
                anchors.fill: parent
                source: house.loadingArt
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                opacity: house.loadingArt.length > 0 ? 1 : 0
            }
            Rectangle {
                anchors.fill: parent
                color: "#99000000"
            }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 22
                text: house.loadingTitle
                color: "#f5f5f7"
                font.family: guide.uiFont
                font.pixelSize: 32
                font.weight: 600
                elide: Text.ElideRight
            }
        }

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 72
            height: 72
            visible: !house.loadingFailed
            RotationAnimation on rotation {
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
                running: house.loading && !house.loadingFailed
            }
            Rectangle {
                anchors.fill: parent
                radius: 36
                color: "transparent"
                border.width: 6
                border.color: "#3a3a3c"
            }
            Rectangle {
                width: 18
                height: 6
                radius: 3
                color: guide.accent
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 640
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: house.loadingFailed ? house.loadingError : house.loadingStatus
            color: house.loadingFailed ? "#ffb4ab" : "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 26
            font.weight: 600
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 420
            height: 8
            radius: 4
            color: "#2c2c30"
            visible: !house.loadingFailed
            Rectangle {
                width: parent.width * Math.max(0.04, Math.min(1, house.loadingProgress))
                height: parent.height
                radius: 4
                color: guide.accent
                Behavior on width {
                    NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: house.loadingFailed
            text: "OK tenta de novo    ·    Voltar fecha isto"
            color: "#aeaeb2"
            font.family: guide.uiFont
            font.pixelSize: 20
        }
    }
}
