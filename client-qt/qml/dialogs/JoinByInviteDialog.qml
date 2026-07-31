import QtQuick
import QtQuick.Controls
import "../components"

DiscordDialog {
    title: "Join by Invite"
    width: 360

    DiscordTextField {
        id: codeField
        width: parent.width
        placeholderText: "Invite code"
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
            text: "Join"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: codeField.text.trim() !== ""
        }
    }

    onAccepted: {
        appState.acceptInvite(codeField.text.trim())
        codeField.text = ""
    }
}
