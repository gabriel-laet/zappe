import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.zappe.app

ApplicationWindow {
    id: root
    width: 1280
    height: 900
    visible: backend.hudVisible
    color: "#0e1014"
    title: "Zappe"

    ZappeBackend { id: backend }

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

    // ApplicationWindow has no `focus` property in Qt 6.11 — remote keys live on Item/FocusScope.
    FocusScope {
        id: remoteInput
        anchors.fill: parent
        focus: true

        Component.onCompleted: forceActiveFocus()

        Keys.onPressed: function(event) {
            if (backend.commandText.length > 0 || commandBar.visible) {
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
            case Qt.Key_Up: backend.moveFocus(-1, 0); break
            case Qt.Key_Down: backend.moveFocus(1, 0); break
            case Qt.Key_Left: backend.moveFocus(0, -1); break
            case Qt.Key_Right: backend.moveFocus(0, 1); break
            case Qt.Key_Return:
            case Qt.Key_Enter: backend.activate(); break
            case Qt.Key_Escape:
            case Qt.Key_Back: backend.back(); break
            case Qt.Key_Home: backend.moveFocus(0, 0); break
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

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 32
            spacing: 20

        RowLayout {
            Layout.fillWidth: true
            Rectangle { width: 6; height: 44; color: "#fa9428" }
            ColumnLayout {
                spacing: 4
                Text {
                    text: "Zappe"
                    font.pixelSize: 36
                    font.weight: Font.DemiBold
                    color: "#ebeff7"
                }
                Text {
                    text: "launcher da sala"
                    font.pixelSize: 16
                    color: "#8c94a6"
                }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                spacing: 4
                Text { text: backend.chromeLine; font.pixelSize: 14; color: "#8c94a6"; horizontalAlignment: Text.AlignRight }
                Text { text: backend.otaLine; font.pixelSize: 14; color: "#8c94a6"; horizontalAlignment: Text.AlignRight }
            }
        }

        Text {
            Layout.fillWidth: true
            text: backend.statusMessage
            font.pixelSize: 18
            color: "#8c94a6"
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Setup: Chrome
            ColumnLayout {
                anchors.centerIn: parent
                visible: backend.screen === 0
                spacing: 24
                width: parent.width * 0.7

                Text {
                    Layout.fillWidth: true
                    text: "Passo 1 — Navegador"
                    font.pixelSize: 28
                    color: "#ebeff7"
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.browserMessage
                    font.pixelSize: 20
                    color: "#8c94a6"
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.distroHint
                    font.pixelSize: 16
                    color: "#7a9ec7"
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.installCommand
                    font.pixelSize: 18
                    font.family: "monospace"
                    color: "#61b8fa"
                    wrapMode: Text.Wrap
                    selectByMouse: true
                }

                RowLayout {
                    spacing: 16
                    ActionButton { label: "Tentar instalar"; onClicked: backend.tryInstallBrowser() }
                    ActionButton { label: "Tentar de novo"; onClicked: backend.retryBrowserDetect() }
                    ActionButton { label: "Continuar"; primary: true; onClicked: backend.continueChromeSetup() }
                }
            }

            // Setup: 1Password
            ColumnLayout {
                anchors.centerIn: parent
                visible: backend.screen === 1
                spacing: 24
                width: parent.width * 0.75

                Text {
                    text: "Passo 2 — 1Password (opcional)"
                    font.pixelSize: 28
                    color: "#ebeff7"
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.onepasswordSummary
                    font.pixelSize: 20
                    color: "#8c94a6"
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.onepasswordAppHint
                    font.pixelSize: 16
                    color: "#8c94a6"
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    spacing: 16
                    ActionButton { label: "Abrir extensão"; onClicked: backend.openOnePasswordExtension() }
                    ActionButton { label: "Pular"; onClicked: backend.skipOnePassword() }
                    ActionButton { label: "Continuar"; primary: true; onClicked: backend.continueOnePassword() }
                }
            }

            // Accounts
            ColumnLayout {
                anchors.fill: parent
                visible: backend.screen === 2
                spacing: 16

                Text {
                    text: "Contas"
                    font.pixelSize: 26
                    color: "#7a9ec7"
                }
                Text {
                    Layout.fillWidth: true
                    text: "Entre uma vez no Chrome do Zappe. 1Password na extensão ajuda."
                    font.pixelSize: 18
                    color: "#ebeff7"
                    wrapMode: Text.WordWrap
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 16
                    Repeater {
                        model: JSON.parse(backend.catalogJson).accounts
                        delegate: Tile {
                            title: modelData.label
                            subtitle: "OK abre login"
                            focused: modelData.focused
                        }
                    }
                }
            }

            // Guide
            Flickable {
                anchors.fill: parent
                visible: backend.screen === 3
                contentHeight: guideCol.height
                clip: true

                Column {
                    id: guideCol
                    width: parent.width
                    spacing: 24

                    Repeater {
                        model: JSON.parse(backend.catalogJson).rows
                        delegate: Column {
                            width: parent.width
                            spacing: 12
                            Text {
                                text: modelData.label
                                font.pixelSize: 22
                                color: "#7a9ec7"
                            }
                            Flow {
                                width: parent.width
                                spacing: 16
                                Repeater {
                                    model: modelData.tiles
                                    delegate: Tile {
                                        title: modelData.title
                                        subtitle: modelData.service
                                        focused: modelData.focused
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: commandBar
            Layout.fillWidth: true
            height: commandBar.visible ? 52 : 0
            visible: backend.commandText.length > 0
            color: "#141820"
            radius: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                text: "> " + backend.commandText + "_"
                font.pixelSize: 20
                font.family: "monospace"
                color: "#61b8fa"
            }
        }
        }
    }

    component ActionButton: Button {
        property string label
        property bool primary: false
        text: label
        font.pixelSize: 20
        padding: 16
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            color: primary ? "#61b8fa" : "#1c2029"
            border.color: primary ? "#61b8fa" : "#3a4254"
            border.width: 2
            radius: 10
        }
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: primary ? "#0e1014" : "#ebeff7"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component Tile: Rectangle {
        property string title
        property string subtitle
        property bool focused: false
        width: 260
        height: 96
        radius: 10
        color: focused ? "#262b38" : "#1c1f28"
        border.width: focused ? 3 : 1
        border.color: focused ? "#61b8fa" : "#2a3040"
        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            Text { text: subtitle; font.pixelSize: 14; color: focused ? "#61b8fa" : "#8c94a6" }
            Text { text: title; font.pixelSize: 20; color: "#ebeff7"; elide: Text.ElideRight; width: parent.width }
        }
    }
}
