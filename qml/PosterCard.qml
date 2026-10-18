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
    required property bool live
    required property bool frame
    required property real progress
    required property int shelfIndex
    required property Flickable view

    readonly property bool rowHot: guide.shelf === shelfIndex && !guide.detailOpen && !guide.menuOpen
                                    && guide.page !== "settings"
    readonly property bool focused: rowHot && guide.index === index
    readonly property bool onScreen: {
        if (!view || view.width <= 0)
            return false
        const screenX = x - view.contentX
        return screenX > -width && screenX < view.width
    }
    // Noise invalidates the rounded-card texture. Keep it on the focused
    // channel and its two neighbors; the rest stay a cached still.
    readonly property bool noiseOn: live && onScreen && rowHot && Math.abs(index - guide.index) <= 1
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
    readonly property real tilt: {
        if (!rowHot || !view)
            return 0
        const screenX = x - view.contentX
        let delta = screenX - 80
        if (delta > 420)
            delta = 420
        if (delta < -220)
            delta = -220
        return -delta / 52
    }

    width: wide ? 432 : 216
    height: wide ? 243 : 324
    z: focused ? 8 : 0

    Item {
        id: visual
        anchors.fill: parent
        transformOrigin: Item.Center
        scale: card.focused ? guide.focusScale * (guide.cardDown ? 0.92 : 1) : 1
        Behavior on scale {
            NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
        }
        transform: [
            Rotation {
                origin.x: visual.width / 2
                origin.y: visual.height / 2
                axis { x: 0; y: 1; z: 0 }
                angle: card.tilt
                Behavior on angle {
                    NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
                }
            },
            Translate {
                x: card.travel * (card.focused ? -12 : -8)
                y: card.focused ? -10 : 0
                Behavior on y {
                    NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
                }
            }
        ]

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
            Behavior on opacity { NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease } }
            Behavior on width {
                NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
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
            Behavior on opacity { NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease } }
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
                    x: (parent.width - width) / 2 - card.travel * (card.focused ? 10 : 16)
                    y: (parent.height - height) / 2
                    source: card.artwork
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    mipmap: true
                }
                Item {
                    anchors.fill: parent
                    visible: card.live && !card.frame
                    clip: true

                    Image {
                        id: grain
                        width: parent.width + 96
                        height: parent.height + 96
                        x: -phase
                        y: -phase * 0.35
                        source: guide.noiseUrl
                        fillMode: Image.Tile
                        smooth: false
                        opacity: 0.55
                        property real phase: (card.index % 5) * 18
                        NumberAnimation on phase {
                            running: card.noiseOn
                            from: (card.index % 5) * 18
                            to: (card.index % 5) * 18 + 96
                            duration: 2800 + (card.index % 4) * 360
                            loops: Animation.Infinite
                        }
                    }
                    Rectangle {
                        width: parent.width * 0.62
                        height: parent.height * 0.9
                        radius: width / 2
                        color: card.color.length ? card.color : "#E85A1B"
                        opacity: 0.55
                        x: parent.width * 0.08 + grain.phase * 0.35
                        y: parent.height * 0.08
                    }
                    Rectangle {
                        width: parent.width * 0.4
                        height: parent.height * 0.7
                        radius: width / 2
                        color: "#f4f1ea"
                        opacity: 0.16
                        x: parent.width * 0.48 - grain.phase * 0.22
                        y: parent.height * 0.2
                    }
                }
                Rectangle {
                    visible: card.wide && !card.live && card.progress > 0.01
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
                cached: !card.noiseOn
            }

            Rectangle {
                visible: card.live
                x: 14
                y: 14
                width: liveLabel.implicitWidth + 22
                height: 28
                radius: 8
                color: "#E23B2F"
                opacity: livePulse
                Text {
                    id: liveLabel
                    anchors.centerIn: parent
                    text: "AO VIVO"
                    color: "#ffffff"
                    font.family: guide.uiFont
                    font.pixelSize: 13
                    font.weight: 700
                    font.letterSpacing: 1.1
                }
                property real livePulse: 1
                SequentialAnimation on livePulse {
                    running: card.live && card.onScreen && !guide.detailOpen && !guide.menuOpen
                             && guide.page !== "settings"
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.55; duration: 760; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1; duration: 760; easing.type: Easing.InOutQuad }
                }
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
                Behavior on opacity { NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease } }
            }
        }
    }
}
