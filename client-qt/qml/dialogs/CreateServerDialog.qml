import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

DiscordDialog {
    title: "Create Server"
    width: 380

    ColumnLayout {
        width: parent.width
        spacing: 12
        DiscordTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "Server name"
        }
        DiscordTextField {
            id: descField
            Layout.fillWidth: true
            placeholderText: "Description (optional)"
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
}
