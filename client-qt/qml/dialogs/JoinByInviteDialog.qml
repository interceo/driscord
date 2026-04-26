import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    title: "Join by Invite"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 320

    TextField {
        id: codeField; width: parent.width
        placeholderText: "Invite code"
        background: Rectangle { color: "#1e1f22"; radius: 4 }
        color: "white"; placeholderTextColor: "#72767d"
    }

    footer: DialogButtonBox {
        Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { text: "Join"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; enabled: codeField.text.trim() !== "" }
    }
    onAccepted: { appState.acceptInvite(codeField.text.trim()); codeField.text = "" }
}
