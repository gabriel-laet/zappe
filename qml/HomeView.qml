import QtQuick

Item {
    id: home
    visible: guide.page !== "settings"

    Item {
        id: hero
        z: 2
        x: 80
        y: 118
        width: parent.width - 160
        height: 112
        opacity: guide.detailOpen ? 0 : 1
        Behavior on opacity {
            NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease }
        }

        Text {
            id: heroTitle
            width: parent.width * 0.78
            text: guide.focusTitle
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 58
            font.weight: 600
            font.letterSpacing: -1.15
            elide: Text.ElideRight
        }
        Row {
            anchors.top: heroTitle.bottom
            anchors.topMargin: 10
            spacing: 16
            Row {
                visible: guide.focusLive
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#E23B2F"
                }
                Text {
                    text: "AO VIVO"
                    color: "#E23B2F"
                    font.family: guide.uiFont
                    font.pixelSize: 16
                    font.weight: 700
                    font.letterSpacing: 1.3
                }
            }
            Text {
                text: {
                    if (guide.shelfCount === 0)
                        return "Your shelves are empty"
                    const service = guide.focusService
                    const shelf = guide.focusShelf
                    if (service.length && shelf.length && service.toLowerCase() !== shelf.toLowerCase())
                        return service + "    ·    " + shelf
                    return shelf.length ? shelf : service
                }
                color: "#aeaeb2"
                font.family: guide.uiFont
                font.pixelSize: 24
            }
        }
    }

    Flickable {
        id: scroller
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        anchors.topMargin: 252
        contentWidth: width
        contentHeight: column.height + 96
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        opacity: guide.detailOpen ? 0.28 : 1
        Behavior on opacity {
            NumberAnimation { duration: Motion.fadeMs; easing.type: Motion.ease }
        }

        property real scrollY: 0
        property bool armed: false
        Behavior on scrollY {
            enabled: scroller.armed
            NumberAnimation { duration: Motion.scrollMs; easing.type: Motion.ease }
        }
        onScrollYChanged: contentY = scrollY

        Column {
            id: column
            width: scroller.width
            spacing: 0
            Repeater {
                id: shelves
                model: guide.shelves
                ShelfRow {}
            }
        }

        function align() {
            if (shelves.count === 0)
                return true
            const item = shelves.itemAt(guide.shelf)
            if (!item || item.height < 40)
                return false
            if (guide.shelf > 0) {
                const prev = shelves.itemAt(guide.shelf - 1)
                if (!prev || item.y < prev.height * 0.5)
                    return false
            }
            const maxY = Math.max(0, contentHeight - height)
            scrollY = Math.max(0, Math.min(maxY, item.y))
            return true
        }
    }

    function kick() {
        layoutRetry.tries = 0
        layoutRetry.running = true
    }

    Connections {
        target: guide
        function onFocusChanged() { home.kick() }
        function onPageChanged() { home.kick() }
    }
    Timer {
        id: layoutRetry
        interval: 16
        repeat: true
        running: true
        property int tries: 0
        onTriggered: {
            tries++
            if ((scroller.align() && tries > 2) || tries > 40) {
                scroller.armed = true
                running = false
            }
        }
    }
}
