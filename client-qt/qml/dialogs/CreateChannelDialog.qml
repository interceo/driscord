import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

DiscordDialog {
    id: root
    objectName: "createChannelDialog"
    title: "Create Channel"
    width: 360

    ColumnLayout {
        width: parent.width
        spacing: 12
        DiscordTextField {
            id: nameField
            objectName: "channelNameField"
            Layout.fillWidth: true
            placeholderText: "Channel name"
            maximumLength: 64
        }
        DiscordComboBox {
            id: kindBox
            objectName: "channelKindBox"
            Layout.fillWidth: true
            model: ["voice", "text"]
        }
    }

    footer: DialogButtonBox {
        leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
        spacing: 8
        background: Rectangle { color: "#2b2d31"; radius: 8 }
        DiscordButton {
            text: "Cancel"; variant: "secondary"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        DiscordButton {
            objectName: "createChannelButton"
            text: "Create"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: nameField.text.trim() !== ""
        }
    }

    onAccepted: {
        appState.createChannel(nameField.text.trim(), kindBox.currentText)
        nameField.text = ""
    }
    onRejected: nameField.text = ""
}
