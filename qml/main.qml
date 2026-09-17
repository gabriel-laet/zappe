import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.zappe.app

ApplicationWindow {
    id: root
    width: 1280
    height: 720
    visible: backend.hud_visible
    color: "#07080c"
    title: "Zappe"

    ZappeBackend {
        id: backend
        Component.onCompleted: tick()
    }

    function uiModel() {
        const raw = backend.catalog_json
        if (!raw)
            return ({ setup: { active: false }, rows: [], accounts: [] })
        try {
            return JSON.parse(raw)
        } catch (e) {
            return ({ setup: { active: false }, rows: [], accounts: [] })
        }
    }

    Timer {
        interval: 40
        running: true
        repeat: true
        onTriggered: {
            backend.tick()
            if (backend.shouldQuit())
                Qt.quit()
        }
    }

    onActiveChanged: if (active) Qt.callLater(remoteInput.forceActiveFocus)
    onVisibleChanged: if (visible) Qt.callLater(remoteInput.forceActiveFocus)

    FocusScope {
        id: remoteInput
        anchors.fill: parent
        focus: true
        Component.onCompleted: forceActiveFocus()

        Keys.onPressed: function(event) {
            const cmd = backend.command_text || ""
            if (cmd.length > 0) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    backend.commandCommit()
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Escape) {
                    backend.commandCancel()
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Backspace) {
                    backend.commandBackspace()
                    event.accepted = true
                    return
                }
                if (event.text && event.text.length > 0) {
                    backend.commandAppend(event.text)
                    event.accepted = true
                }
                return
            }

            switch (event.key) {
            case Qt.Key_Up: backend.move_focus(-1, 0); break
            case Qt.Key_Down: backend.move_focus(1, 0); break
            case Qt.Key_Left: backend.move_focus(0, -1); break
            case Qt.Key_Right: backend.move_focus(0, 1); break
            case Qt.Key_Return:
            case Qt.Key_Enter: backend.activate(); break
            case Qt.Key_Escape:
            case Qt.Key_Back: backend.back(); break
            case Qt.Key_Home: backend.move_focus(0, 0); break
            case Qt.Key_Space:
            case Qt.Key_MediaPlay:
            case Qt.Key_MediaPause:
            case Qt.Key_MediaTogglePlayPause:
                backend.playPause(); break
            case Qt.Key_Slash: backend.openCommand(); break
            case Qt.Key_Q: backend.quit(); break
            case Qt.Key_H: backend.toggleHud(); break
            default: return
            }
            event.accepted = true
        }

        // —— First-run setup (Apple TV style) ——
        Item {
            anchors.fill: parent
            visible: uiModel().setup.active
            anchors.margins: 72

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width, 920)
                spacing: 40

                Text {
                    width: parent.width
                    text: uiModel().setup.title || ""
                    font.pixelSize: 42
                    font.weight: Font.DemiBold
                    color: "#f2f4f8"
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: uiModel().setup.subtitle || ""
                    font.pixelSize: 22
                    color: "#9aa3b5"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.25
                }
                Text {
                    width: parent.width
                    visible: uiModel().setup.show_install_command === true
                    text: uiModel().setup.install_command || ""
                    font.pixelSize: 16
                    font.family: "monospace"
                    color: "#6b9fd4"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.85
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 20
                    Repeater {
                        model: uiModel().setup.choices || []
                        delegate: FocusCard {
                            label: modelData.label
                            primary: modelData.primary
                            focused: modelData.focused
                            width: 240
                            height: 72
                        }
                    }
                }
            }
        }

        // —— Accounts onboarding ——
        Item {
            anchors.fill: parent
            visible: backend.screen === 2
            anchors.margins: 56

            Column {
                anchors.fill: parent
                spacing: 28
                Text {
                    text: "Contas"
                    font.pixelSize: 34
                    font.weight: Font.DemiBold
                    color: "#f2f4f8"
                }
                Text {
                    width: parent.width
                    text: "Entre uma vez em cada serviço. OK abre o login no Chrome."
                    font.pixelSize: 20
                    color: "#9aa3b5"
                    wrapMode: Text.WordWrap
                }
                Flickable {
                    width: parent.width
                    height: parent.height - 120
                    contentWidth: accountsRow.width
                    clip: true
                    interactive: false
                    Row {
                        id: accountsRow
                        spacing: 20
                        Repeater {
                            model: uiModel().accounts || []
                            delegate: PosterTile {
                                title: modelData.label
                                subtitle: ""
                                focused: modelData.focused
                                appStyle: true
                            }
                        }
                    }
                }
            }
        }

        // —— Home / guide shelves ——
        Item {
            anchors.fill: parent
            visible: backend.screen === 3
            anchors.topMargin: 40
            anchors.leftMargin: 56
            anchors.rightMargin: 56
            anchors.bottomMargin: 32

            Text {
                id: brand
                text: "Zappe"
                font.pixelSize: 22
                font.weight: Font.Medium
                color: "#6d7689"
            }

            Flickable {
                anchors.top: brand.bottom
                anchors.topMargin: 24
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                contentHeight: homeShelves.height
                clip: true
                interactive: false

                Column {
                    id: homeShelves
                    width: parent.width
                    spacing: 36

                    Repeater {
                        model: uiModel().rows || []
                        delegate: Column {
                            width: parent.width
                            spacing: 14
                            Text {
                                text: modelData.label
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                color: "#8b95a8"
                            }
                            Row {
                                spacing: 16
                                Repeater {
                                    model: modelData.tiles
                                    delegate: PosterTile {
                                        title: modelData.title
                                        subtitle: modelData.service
                                        focused: modelData.focused
                                        appStyle: modelData.service === "NETFLIX"
                                            || modelData.service === "PRIME"
                                            || modelData.service === "DISNEY+"
                                            || modelData.service === "YOUTUBE"
                                            || modelData.title === "Contas"
                                            || modelData.title === "TV ao vivo"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Toast (errors only)
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40
            width: Math.min(parent.width - 80, toastText.implicitWidth + 48)
            height: toastText.implicitHeight + 28
            radius: 12
            color: "#1a1f2a"
            border.color: "#3d4a63"
            visible: (backend.toast_message || "").length > 0
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }

            Text {
                id: toastText
                anchors.centerIn: parent
                width: parent.width - 32
                text: backend.toast_message || ""
                font.pixelSize: 18
                color: "#e8ecf4"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    component FocusCard: Rectangle {
        id: card
        property string label
        property bool primary: false
        property bool focused: false

        radius: 14
        color: primary ? (focused ? "#e8f2ff" : "#c5dcf5") : (focused ? "#2a3142" : "#151922")
        border.width: focused ? 3 : 1
        border.color: focused ? "#ffffff" : "#2e3648"
        scale: focused ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        Text {
            anchors.centerIn: parent
            text: card.label
            font.pixelSize: 20
            font.weight: card.primary ? Font.DemiBold : Font.Normal
            color: card.primary ? "#0a0c10" : "#eef1f6"
        }
    }

    component PosterTile: Rectangle {
        id: tile
        property string title
        property string subtitle
        property bool focused: false
        property bool appStyle: false

        width: appStyle ? 120 : 200
        height: appStyle ? 120 : 112
        radius: appStyle ? 28 : 14
        color: focused ? "#2c3344" : "#141820"
        border.width: focused ? 3 : 1
        border.color: focused ? "#ffffff" : "#252b38"
        scale: focused ? 1.08 : 1
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        Column {
            anchors.centerIn: parent
            width: parent.width - 16
            spacing: 6
            Text {
                width: parent.width
                text: tile.title
                font.pixelSize: appStyle ? 15 : 17
                font.weight: Font.DemiBold
                color: "#f0f3f9"
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !appStyle && tile.subtitle.length > 0
                text: tile.subtitle
                font.pixelSize: 12
                color: focused ? "#a8c8f0" : "#7a8496"
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}
