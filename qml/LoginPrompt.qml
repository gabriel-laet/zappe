import QtQuick

Rectangle {
    anchors.fill: parent
    visible: house.promptOpen
    color: "#cc000000"

    Rectangle {
        anchors.centerIn: parent
        width: 860
        height: column.implicitHeight + 72
        radius: 28
        color: "#141416"
        border.width: 2
        border.color: guide.accent

        Column {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 36
            spacing: 16

            Text {
                text: house.promptService
                color: guide.accent
                font.family: guide.uiFont
                font.pixelSize: 20
                font.weight: 600
            }
            Text {
                text: house.promptText
                color: "#f5f5f7"
                font.family: guide.uiFont
                font.pixelSize: 36
                font.weight: 600
                width: parent.width
                wrapMode: Text.WordWrap
            }
            Text {
                text: house.promptHint
                color: "#8e8e93"
                font.family: guide.uiFont
                font.pixelSize: 20
                width: parent.width
                wrapMode: Text.WordWrap
            }
            Rectangle {
                width: parent.width
                height: 72
                radius: 16
                color: "#000000"
                border.color: "#2c2c30"
                TextInput {
                    id: field
                    anchors.fill: parent
                    anchors.margins: 18
                    text: house.draft
                    color: "#f5f5f7"
                    font.family: guide.uiFont
                    font.pixelSize: 28
                    echoMode: house.promptSecret ? TextInput.Password : TextInput.Normal
                    inputMethodHints: house.promptField === "code" ? Qt.ImhDigitsOnly : Qt.ImhNone
                    onTextChanged: house.draft = text
                    Keys.onReturnPressed: house.submitPrompt(text)
                    Keys.onEscapePressed: house.cancelPrompt()
                }
            }
            Text {
                text: "OK envia. Voltar cancela."
                color: "#8e8e93"
                font.family: guide.uiFont
                font.pixelSize: 18
            }
        }
    }

    onVisibleChanged: {
        if (visible)
            field.forceActiveFocus()
    }
}
