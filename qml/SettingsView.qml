import QtQuick

Item {
    opacity: guide.page === "settings" && !guide.detailOpen ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity {
        NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
    }

    Column {
        x: 96
        y: 168
        spacing: 22
        width: 980

        Text {
            text: "Ajustes"
            color: "#f5f5f7"
            font.family: guide.uiFont
            font.pixelSize: 56
            font.weight: 600
            font.letterSpacing: -1.1
        }
        Text {
            text: "1080p é o padrão. 4K entra no próximo início do kiosk. A marca vem do arquivo do aparelho."
            color: "#8e8e93"
            font.family: guide.uiFont
            font.pixelSize: 22
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: 920
            height: 128
            radius: 24
            color: guide.settingsIndex === 0 ? "#1c1c1e" : "#141416"
            border.width: guide.settingsIndex === 0 ? 3 : 1
            border.color: guide.settingsIndex === 0 ? "#f5f5f7" : "#2c2c30"

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    text: "Resolução"
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 28
                    font.weight: 600
                }
                Text {
                    text: "Aplica no próximo início"
                    color: "#8e8e93"
                    font.family: guide.uiFont
                    font.pixelSize: 18
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                Repeater {
                    model: ["1080p", "4K"]
                    Rectangle {
                        required property string modelData
                        width: label.implicitWidth + 36
                        height: 52
                        radius: 26
                        color: guide.resolution === modelData ? guide.accent : "#2c2c2e"
                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: modelData
                            color: guide.resolution === modelData ? guide.accentInk : "#f5f5f7"
                            font.family: guide.uiFont
                            font.pixelSize: 22
                            font.weight: 600
                        }
                    }
                }
            }
        }

        Rectangle {
            width: 920
            height: 128
            radius: 24
            color: guide.settingsIndex === 1 ? "#1c1c1e" : "#141416"
            border.width: guide.settingsIndex === 1 ? 3 : 1
            border.color: guide.settingsIndex === 1 ? "#f5f5f7" : "#2c2c30"

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    text: "Marca"
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 28
                    font.weight: 600
                }
                Text {
                    text: guide.brandingOn
                          ? (guide.brandCustom ? guide.loadedBrandName : "Sem branding.json")
                          : "Padrão Zappe"
                    color: "#8e8e93"
                    font.family: guide.uiFont
                    font.pixelSize: 18
                    width: 560
                    elide: Text.ElideRight
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                spacing: 16
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: guide.brandName
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 22
                    font.weight: 600
                }
                Rectangle {
                    width: 64
                    height: 36
                    radius: 18
                    color: guide.brandingOn ? guide.accent : "#3a3a3c"
                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        y: 4
                        x: guide.brandingOn ? 32 : 4
                        color: "#f5f5f7"
                        Behavior on x {
                            SpringAnimation {
                                spring: 3.2
                                damping: 0.62
                                mass: 0.6
                                epsilon: 0.2
                            }
                        }
                    }
                }
            }
        }
    }
}
