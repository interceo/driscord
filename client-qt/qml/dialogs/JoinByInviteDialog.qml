import QtQuick
import QtQuick.Controls
import "../components"

DiscordDialog {
    id: root
    objectName: "joinByInviteDialog"
    title: "Join by Invite"
    width: 360

    DiscordTextField {
        id: codeField
        objectName: "inviteCodeField"
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
            objectName: "joinByInviteButton"
            text: "Join"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: codeField.text.trim() !== ""
        }
    }

    onAccepted: {
        appState.acceptInvite(codeField.text.trim())
        codeField.text = ""
    }
    onRejected: codeField.text = ""
}
