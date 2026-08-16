import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

DiscordDialog {
    id: root
    objectName: "createServerDialog"
    title: "Create Server"
    width: 380

    ColumnLayout {
        width: parent.width
        spacing: 12
        DiscordTextField {
            id: nameField
            objectName: "serverNameField"
            Layout.fillWidth: true
            placeholderText: "Server name"
            maximumLength: 64
        }
        DiscordTextField {
            id: descField
            objectName: "serverDescriptionField"
            Layout.fillWidth: true
            placeholderText: "Description (optional)"
            maximumLength: 256
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
            objectName: "createServerButton"
            text: "Create"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: nameField.text.trim() !== ""
        }
    }

    onAccepted: {
        appState.createServer(nameField.text.trim(), descField.text.trim())
        nameField.text = ""
        descField.text = ""
    }
    onRejected: {
        nameField.text = ""
        descField.text = ""
    }
}
