import QtQuick

Item {
    id: shelf
    required property int index
    required property string title
    required property string status
    required property string message
    required property bool wide
    required property var rows

    readonly property int shelfIndex: index
    readonly property bool rowFocused: guide.shelf === shelfIndex && !guide.detailOpen
    readonly property int cardH: wide ? 243 : 324
    readonly property int gutter: 80

    width: parent ? parent.width : 0
    height: titleBlock.height + (cardRepeater.count > 0 ? flick.height + 12 : empty.height + 12)

    Item {
        id: titleBlock
        width: parent.width
        height: label.implicitHeight + (note.visible ? note.implicitHeight + 6 : 0)
        Text {
            id: label
            x: shelf.gutter
            text: shelf.title
            color: shelf.rowFocused ? "#f5f5f7" : "#6e6e73"
            font.family: guide.uiFont
            font.pixelSize: 22
            font.weight: 500
            Behavior on color {
                ColorAnimation { duration: 200 }
            }
        }
        Text {
            id: note
            x: shelf.gutter
            anchors.top: label.bottom
            anchors.topMargin: 4
            visible: shelf.status !== "ok" && shelf.message.length > 0
            text: shelf.message
            color: guide.accent
            font.family: guide.uiFont
            font.pixelSize: 18
            width: parent.width - shelf.gutter * 2
            elide: Text.ElideRight
        }
    }

    Flickable {
        id: flick
        anchors.top: titleBlock.bottom
        anchors.topMargin: 12
        width: parent.width
        height: shelf.cardH + 108
        visible: cardRepeater.count > 0
        contentWidth: strip.width
        contentHeight: height
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds

        property real scrollX: 0
        property bool armed: false
        Behavior on scrollX {
            enabled: flick.armed
            SpringAnimation {
                spring: 1.85
                damping: 0.73
                mass: 0.9
                epsilon: 0.6
            }
        }
        onScrollXChanged: contentX = scrollX

        Row {
            id: strip
            y: 44
            spacing: 26
            Item {
                width: shelf.gutter
                height: 1
            }
            Repeater {
                id: cardRepeater
                model: shelf.rows
                PosterCard {
                    shelfIndex: shelf.shelfIndex
                    view: flick
                }
            }
            Item {
                width: shelf.gutter
                height: 1
            }
        }

        // True once the leading gutter has a real x, so the first align
        // does not lock contentX at 0 before the Row has been positioned.
        function align() {
            if (!shelf.rowFocused)
                return true
            if (cardRepeater.count === 0) {
                scrollX = 0
                return true
            }
            const first = cardRepeater.itemAt(0)
            const item = cardRepeater.itemAt(guide.index)
            if (!first || !item || first.x < shelf.gutter - 1)
                return false
            const maxX = Math.max(0, strip.width - width)
            scrollX = Math.max(0, Math.min(maxX, item.x - shelf.gutter))
            return true
        }
    }

    Item {
        id: empty
        visible: cardRepeater.count === 0
        anchors.top: titleBlock.bottom
        anchors.topMargin: 12
        x: shelf.gutter
        width: 440
        height: 150
        Rectangle {
            anchors.fill: parent
            radius: 18
            color: "#141416"
            border.color: "#2c2c30"
            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: shelf.message.length ? shelf.message : "Nothing here yet"
                color: "#8e8e93"
                font.family: guide.uiFont
                font.pixelSize: 22
            }
        }
    }

    Connections {
        target: guide
        function onFocusChanged() {
            flick.align()
        }
    }
    Timer {
        interval: 16
        repeat: true
        running: true
        property int tries: 0
        onTriggered: {
            tries++
            if (flick.align() || tries > 40) {
                flick.armed = true
                running = false
            }
        }
    }
}
