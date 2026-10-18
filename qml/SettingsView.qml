import QtQuick

Item {
    opacity: guide.page === "settings" && !guide.detailOpen ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity {
        NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
    }

    Text {
        x: 96
        y: 64
        text: "Ajustes"
        color: "#f5f5f7"
        font.family: guide.uiFont
        font.pixelSize: 56
        font.weight: 600
        font.letterSpacing: -1.1
    }
    Text {
        x: 96
        y: 132
        width: 980
        text: "1080p é o padrão. A seção TV fala com a Samsung UN43TU7020 por Device Connect."
        color: "#8e8e93"
        font.family: guide.uiFont
        font.pixelSize: 22
        wrapMode: Text.WordWrap
    }

    Flickable {
        id: flick
        x: 96
        y: 196
        width: 980
        height: parent.height - y - 36
        contentWidth: width
        contentHeight: column.height
        interactive: false
        clip: true
        contentY: {
            var idx = guide.settingsIndex
            var item = idx === 0 ? resCard : idx === 1 ? brandCard : idx === 2 ? statusCard
                       : idx === 3 ? powerCard : idx === 4 ? volCard : idx === 5 ? muteCard : hdmiCard
            var target = item.y - 12
            var maxY = Math.max(0, contentHeight - height)
            return Math.max(0, Math.min(target, maxY))
        }
        Behavior on contentY {
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }

        Column {
            id: column
            width: 920
            spacing: 14

            Rectangle {
                id: resCard
                width: 920
                height: 108
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
                id: brandCard
                width: 920
                height: 108
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
                        width: 520
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

            Text {
                text: "TV"
                color: "#f5f5f7"
                font.family: guide.uiFont
                font.pixelSize: 28
                font.weight: 600
                topPadding: 8
            }

            Rectangle {
                id: statusCard
                width: 920
                height: 88
                radius: 22
                color: guide.settingsIndex === 2 ? "#1c1c1e" : "#141416"
                border.width: guide.settingsIndex === 2 ? 3 : 1
                border.color: guide.settingsIndex === 2 ? "#f5f5f7" : "#2c2c30"
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "Estado"
                        color: "#f5f5f7"
                        font.family: guide.uiFont
                        font.pixelSize: 26
                        font.weight: 600
                    }
                    Text {
                        text: "OK consulta de novo"
                        color: "#8e8e93"
                        font.family: guide.uiFont
                        font.pixelSize: 16
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: tv.statusLine
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 22
                    font.weight: 600
                }
            }

            Rectangle {
                id: powerCard
                width: 920
                height: 88
                radius: 22
                color: guide.settingsIndex === 3 ? "#1c1c1e" : "#141416"
                border.width: guide.settingsIndex === 3 ? 3 : 1
                border.color: guide.settingsIndex === 3 ? "#f5f5f7" : "#2c2c30"
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "Ligar / desligar"
                        color: "#f5f5f7"
                        font.family: guide.uiFont
                        font.pixelSize: 26
                        font.weight: 600
                    }
                    Text {
                        text: "OK alterna. O botão Power do controle faz o mesmo."
                        color: "#8e8e93"
                        font.family: guide.uiFont
                        font.pixelSize: 16
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: tv.powerLabel
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 22
                    font.weight: 600
                }
            }

            Rectangle {
                id: volCard
                width: 920
                height: 88
                radius: 22
                color: guide.settingsIndex === 4 ? "#1c1c1e" : "#141416"
                border.width: guide.settingsIndex === 4 ? 3 : 1
                border.color: guide.settingsIndex === 4 ? "#f5f5f7" : "#2c2c30"
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "Volume"
                        color: "#f5f5f7"
                        font.family: guide.uiFont
                        font.pixelSize: 26
                        font.weight: 600
                    }
                    Text {
                        text: "Esquerda diminui, direita aumenta"
                        color: "#8e8e93"
                        font.family: guide.uiFont
                        font.pixelSize: 16
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: tv.volumeLabel
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 22
                    font.weight: 600
                }
            }

            Rectangle {
                id: muteCard
                width: 920
                height: 88
                radius: 22
                color: guide.settingsIndex === 5 ? "#1c1c1e" : "#141416"
                border.width: guide.settingsIndex === 5 ? 3 : 1
                border.color: guide.settingsIndex === 5 ? "#f5f5f7" : "#2c2c30"
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "Mudo"
                        color: "#f5f5f7"
                        font.family: guide.uiFont
                        font.pixelSize: 26
                        font.weight: 600
                    }
                    Text {
                        text: "OK envia mudo para a TV"
                        color: "#8e8e93"
                        font.family: guide.uiFont
                        font.pixelSize: 16
                    }
                }
            }

            Rectangle {
                id: hdmiCard
                width: 920
                height: 88
                radius: 22
                color: guide.settingsIndex === 6 ? "#1c1c1e" : "#141416"
                border.width: guide.settingsIndex === 6 ? 3 : 1
                border.color: guide.settingsIndex === 6 ? "#f5f5f7" : "#2c2c30"
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "Entrada HDMI"
                        color: "#f5f5f7"
                        font.family: guide.uiFont
                        font.pixelSize: 26
                        font.weight: 600
                    }
                    Text {
                        text: "Laterais escolhem, OK envia"
                        color: "#8e8e93"
                        font.family: guide.uiFont
                        font.pixelSize: 16
                    }
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Repeater {
                        model: [1, 2, 3]
                        Rectangle {
                            required property int modelData
                            width: 64
                            height: 44
                            radius: 22
                            color: tv.hdmiPort === modelData ? guide.accent : "#2c2c2e"
                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: tv.hdmiPort === modelData ? guide.accentInk : "#f5f5f7"
                                font.family: guide.uiFont
                                font.pixelSize: 22
                                font.weight: 600
                            }
                        }
                    }
                }
            }
        }
    }
}
