import QtQuick
import Qt5Compat.GraphicalEffects

Item {
    id: card
    required property int index
    required property string title
    required property string service
    required property string artwork
    required property string color
    required property bool wide
    required property real progress
    required property int shelfIndex
    required property Flickable view

    readonly property bool focused: guide.shelf === shelfIndex && guide.index === index && !guide.detailOpen
    readonly property real travel: {
        if (!view || view.width <= 0)
            return 0
        const center = x + width / 2
        const mid = view.contentX + view.width / 2
        let t = (center - mid) / (view.width * 0.55)
        if (t < -1)
            t = -1
        if (t > 1)
            t = 1
        return t
    }

    width: wide ? 432 : 216
    height: wide ? 243 : 324
    z: focused ? 8 : 0

    Item {
        id: visual
        anchors.fill: parent
        transformOrigin: Item.Center
        scale: card.focused ? guide.focusScale : 1
        Behavior on scale {
            SpringAnimation {
                spring: 2.4
                damping: 0.72
                mass: 0.85
                epsilon: 0.0008
            }
        }
        transform: Translate {
            x: card.travel * -8
            y: card.focused ? -10 : 0
            Behavior on y {
                SpringAnimation {
                    spring: 2.2
                    damping: 0.74
                    mass: 0.8
                    epsilon: 0.2
                }
            }
        }

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -16
            width: parent.width * (card.focused ? 1.2 : 1.02)
            height: parent.height * 0.7
            source: guide.shadowUrl
            fillMode: Image.Stretch
            smooth: true
            opacity: card.focused ? 0.95 : 0.4
            z: -2
            Behavior on opacity { NumberAnimation { duration: 180 } }
            Behavior on width {
                SpringAnimation { spring: 2.2; damping: 0.8; epsilon: 0.5 }
            }
        }

        Image {
            anchors.centerIn: parent
            width: parent.width * 1.7
            height: parent.height * 1.75
            source: guide.glowUrl
            fillMode: Image.Stretch
            smooth: true
            opacity: card.focused ? 0.92 : 0
            z: -3
            Behavior on opacity { NumberAnimation { duration: 220 } }
        }

        Item {
            id: face
            anchors.fill: parent

            Item {
                id: stack
                anchors.fill: parent
                visible: false

                Rectangle {
                    anchors.fill: parent
                    color: card.color.length ? card.color : "#1c1c1e"
                }
                Image {
                    width: parent.width * 1.12
                    height: parent.height * 1.12
                    x: (parent.width - width) / 2 - card.travel * 18
                    y: (parent.height - height) / 2
                    source: card.artwork
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    mipmap: true
                }
                Rectangle {
                    visible: card.wide && card.progress > 0.01
                    height: 5
                    width: parent.width * card.progress
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    color: "#f5f5f7"
                }
            }

            Rectangle {
                id: mask
                anchors.fill: parent
                radius: 18
                visible: false
                color: "black"
            }

            OpacityMask {
                anchors.fill: parent
                source: stack
                maskSource: mask
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: 21
                color: "transparent"
                border.width: card.focused ? 3 : 0
                border.color: "#f5f5f7"
                opacity: card.focused ? 0.95 : 0
                antialiasing: true
                Behavior on opacity { NumberAnimation { duration: 160 } }
            }
        }
    }
}
