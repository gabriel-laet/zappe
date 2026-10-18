import QtQuick

Item {
    id: detail
    anchors.fill: parent
    visible: opacity > 0.01
    opacity: guide.detailOpen ? 1 : 0
    Behavior on opacity {
        NumberAnimation {
            duration: Motion.screenMs
            easing.type: Motion.ease
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#A6000000"
    }

    Column {
        x: 88
        y: 148
        spacing: 18
        width: 1040

        Text {
            visible: guide.focusLive
            text: "AO VIVO"
            color: "#E23B2F"
            font.family: guide.uiFont
            font.pixelSize: 16
            font.weight: 700
            font.letterSpacing: 1.6
        }
        Text {
            visible: !guide.focusLive && guide.focusService.length > 0
            text: guide.focusService.toUpperCase()
            color: guide.accent
            font.family: guide.uiFont
            font.pixelSize: 18
            font.weight: 600
            font.letterSpacing: 2.4
        }
        Text {
            width: parent.width
            text: guide.focusTitle
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 64
            font.weight: 600
            font.letterSpacing: -1.4
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            visible: guide.focusShelf.length > 0
                     && guide.focusShelf.toLowerCase() !== guide.focusService.toLowerCase()
            text: guide.focusShelf
            color: "#aeaeb2"
            font.family: guide.uiFont
            font.pixelSize: 24
        }
        Text {
            width: 720
            text: guide.focusLive
                  ? "Canal no ar. A prévia é um desenho, sem vídeo."
                  : "No catálogo local. Este guia só navega."
            color: "#d1d1d6"
            font.family: guide.uiFont
            font.pixelSize: 22
            wrapMode: Text.WordWrap
        }
        Rectangle {
            visible: !guide.focusLive && guide.focusProgress > 0.01
            width: 380
            height: 6
            radius: 3
            color: "#33ffffff"
            Rectangle {
                width: parent.width * guide.focusProgress
                height: parent.height
                radius: 3
                color: "#f5f5f7"
            }
        }

        Row {
            id: actions
            spacing: 16
            topPadding: 8

            Rectangle {
                id: watch
                width: 196
                height: 64
                radius: 32
                color: guide.detailZone === 0 && guide.detailButton === 0 ? guide.accent : "#2a2a2e"
                border.width: guide.detailZone === 0 && guide.detailButton === 0 ? 0 : 0
                scale: (guide.detailZone === 0 && guide.detailButton === 0 ? watchScale : 1)
                       * (guide.cardDown && guide.detailZone === 0 && guide.detailButton === 0 ? 0.94 : 1)
                property real watchScale: 1
                Text {
                    anchors.centerIn: parent
                    text: "Assistir"
                    color: guide.detailZone === 0 && guide.detailButton === 0 ? guide.accentInk : "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 24
                    font.weight: 600
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: 35
                    color: "transparent"
                    border.width: guide.detailZone === 0 && guide.detailButton === 0 ? 3 : 0
                    border.color: "#f5f5f7"
                }
            }

            Rectangle {
                id: trailer
                width: 176
                height: 64
                radius: 32
                color: "#2a2a2e"
                scale: guide.detailZone === 0 && guide.detailButton === 1 ? trailerScale : 1
                property real trailerScale: 1
                Text {
                    anchors.centerIn: parent
                    text: "Trailer"
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 24
                    font.weight: 600
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: 35
                    color: "transparent"
                    border.width: guide.detailZone === 0 && guide.detailButton === 1 ? 3 : 0
                    border.color: "#f5f5f7"
                }
            }

            Rectangle {
                id: mine
                width: mineLabel.implicitWidth + knob.width + 48
                height: 64
                radius: 32
                color: guide.saved ? guide.accent : "#2a2a2e"
                scale: guide.detailZone === 0 && guide.detailButton === 2 ? mineScale : 1
                property real mineScale: 1
                Row {
                    anchors.centerIn: parent
                    spacing: 12
                    Text {
                        id: mineLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: guide.saved ? "Na lista" : "Minha lista"
                        color: guide.saved ? guide.accentInk : "#f5f5f7"
                        font.family: guide.uiFont
                        font.pixelSize: 24
                        font.weight: 600
                    }
                    Rectangle {
                        id: knob
                        width: 52
                        height: 30
                        radius: 15
                        anchors.verticalCenter: parent.verticalCenter
                        color: guide.saved ? "#ffffff" : "#3a3a3c"
                        Rectangle {
                            width: 22
                            height: 22
                            radius: 11
                            y: 4
                            x: guide.saved ? 26 : 4
                            color: guide.saved ? guide.accent : "#f5f5f7"
                            Behavior on x {
                                NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
                            }
                        }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: 35
                    color: "transparent"
                    border.width: guide.detailZone === 0 && guide.detailButton === 2 ? 3 : 0
                    border.color: "#f5f5f7"
                }
            }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 36
        spacing: 14

        Text {
            leftPadding: 88
            text: "Episódios"
            color: guide.detailZone === 1 ? "#f5f5f7" : "#8e8e93"
            font.family: guide.uiFont
            font.pixelSize: 22
            font.weight: 600
        }

        Flickable {
            id: eps
            x: 0
            width: parent.width
            height: 196
            contentWidth: epRow.width + 176
            contentHeight: height
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds

            property real scrollX: 0
            Behavior on scrollX {
                NumberAnimation { duration: Motion.scrollMs; easing.type: Motion.ease }
            }
            onScrollXChanged: contentX = scrollX

            Row {
                id: epRow
                x: 88
                y: 16
                spacing: 18
                Repeater {
                    id: epRep
                    model: guide.episodeCount
                    Item {
                        required property int index
                        width: 292
                        height: 164
                        readonly property bool hot: guide.detailZone === 1 && guide.episode === index

                        Rectangle {
                            anchors.fill: parent
                            radius: 16
                            color: "#141416"
                            scale: parent.hot ? 1.04 : 1
                            transformOrigin: Item.Bottom
                            border.width: parent.hot ? 3 : 0
                            border.color: "#f5f5f7"
                            clip: true
                            Behavior on scale {
                                NumberAnimation { duration: Motion.focusMs; easing.type: Motion.ease }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                height: 96
                                color: {
                                    const colors = ["#1B3A4B", "#C4562A", "#245C73", "#3F8F6B", "#5C4B8A", "#1F7A78"]
                                    return colors[index % colors.length]
                                }
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 78
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#00141416" }
                                    GradientStop { position: 0.45; color: "#CC141416" }
                                    GradientStop { position: 1.0; color: "#F2141416" }
                                }
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                anchors.bottomMargin: 16
                                text: (index + 1) + "   " + guide.episodeTitle(index)
                                color: "#f5f5f7"
                                font.family: guide.uiFont
                                font.pixelSize: 22
                                font.weight: 600
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            function align() {
                if (!guide.detailOpen || guide.detailZone !== 1)
                    return true
                const item = epRep.itemAt(guide.episode)
                if (!item || item.width < 10)
                    return false
                const maxX = Math.max(0, contentWidth - width)
                scrollX = Math.max(0, Math.min(maxX, item.x - 88))
                return true
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 88
        anchors.bottomMargin: 8
        visible: false
        text: "Esc    Voltar"
        color: "#8e8e93"
        font.family: guide.uiFont
        font.pixelSize: 18
    }

    Connections {
        target: guide
        function onDetailChanged() { eps.align() }
        function onActivated() {
            if (guide.detailZone !== 0)
                return
            if (guide.detailButton === 0)
                watchPulse.restart()
            else if (guide.detailButton === 1)
                trailerPulse.restart()
            else
                minePulse.restart()
        }
    }
    Timer {
        interval: 16
        repeat: true
        running: guide.detailOpen
        property int tries: 0
        onRunningChanged: {
            if (running)
                tries = 0
        }
        onTriggered: {
            tries++
            if (eps.align() || tries > 24)
                running = false
        }
    }

    SequentialAnimation {
        id: watchPulse
        NumberAnimation { target: watch; property: "watchScale"; to: 0.94; duration: Motion.pressMs }
        NumberAnimation { target: watch; property: "watchScale"; to: 1; duration: Motion.releaseMs; easing.type: Motion.ease }
    }
    SequentialAnimation {
        id: trailerPulse
        NumberAnimation { target: trailer; property: "trailerScale"; to: 0.94; duration: Motion.pressMs }
        NumberAnimation { target: trailer; property: "trailerScale"; to: 1; duration: Motion.releaseMs; easing.type: Motion.ease }
    }
    SequentialAnimation {
        id: minePulse
        NumberAnimation { target: mine; property: "mineScale"; to: 0.94; duration: Motion.pressMs }
        NumberAnimation { target: mine; property: "mineScale"; to: 1; duration: Motion.releaseMs; easing.type: Motion.ease }
    }
}
