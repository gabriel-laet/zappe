import QtQuick

Item {
    visible: guide.tvTest

    Rectangle {
        anchors.fill: parent
        color: "#0c0c0e"
    }

    Text {
        x: 96
        y: 48
        text: "Testar TV"
        color: "#f5f5f7"
        font.family: guide.uiFont
        font.pixelSize: 52
        font.weight: 600
        font.letterSpacing: -1
    }
    Text {
        x: 96
        y: 112
        width: 1200
        text: "Voltar sai desta tela. Cada tecla fica na lista e no kiosk.log."
        color: "#8e8e93"
        font.family: guide.uiFont
        font.pixelSize: 22
    }

    Row {
        x: 96
        y: 168
        width: 1728
        spacing: 28

        Column {
            width: 520
            spacing: 10
            Text { text: "Ligação  " + tv.scheme; color: "#f5f5f7"; font.family: guide.uiFont; font.pixelSize: 26; font.weight: 600 }
            Text { text: "Token  " + (tv.tokenPresent ? "sim" : "não"); color: "#f5f5f7"; font.family: guide.uiFont; font.pixelSize: 26; font.weight: 600 }
            Text {
                width: 520
                text: "Último erro  " + (tv.lastError.length > 0 ? tv.lastError : "nenhum")
                color: "#f5f5f7"
                font.family: guide.uiFont
                font.pixelSize: 26
                font.weight: 600
                wrapMode: Text.WordWrap
            }
        }
        Column {
            width: 420
            spacing: 10
            Text { text: "Força  " + tv.powerLabel; color: "#f5f5f7"; font.family: guide.uiFont; font.pixelSize: 26; font.weight: 600 }
            Text { text: "Volume  " + tv.volumeLabel; color: "#f5f5f7"; font.family: guide.uiFont; font.pixelSize: 26; font.weight: 600 }
            Text { text: "Mudo  " + tv.muteLabel; color: "#f5f5f7"; font.family: guide.uiFont; font.pixelSize: 26; font.weight: 600 }
        }
    }

    Row {
        x: 96
        y: 320
        spacing: 16
        Repeater {
            model: ["Reparar", "Testar mudo", "Vol+", "Vol-", "HDMI"]
            Rectangle {
                required property int index
                required property string modelData
                width: index === 0 ? 220 : 180
                height: 72
                radius: 22
                color: guide.tvTestIndex === index ? "#1c1c1e" : "#141416"
                border.width: guide.tvTestIndex === index ? 3 : 1
                border.color: guide.tvTestIndex === index ? "#f5f5f7" : "#2c2c30"
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 24
                    font.weight: 600
                }
            }
        }
    }

    ListView {
        x: 96
        y: 420
        width: 1728
        height: parent.height - y - 36
        clip: true
        spacing: 8
        model: house.tvLog
        delegate: Text {
            required property string modelData
            width: 1728
            text: modelData
            color: "#f5f5f7"
            font.family: guide.monoFont
            font.pixelSize: 20
            wrapMode: Text.Wrap
        }
    }
}
