import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    title: "Create Server"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 340

    ColumnLayout {
        width: parent.width; spacing: 12
        TextField {
            id: nameField; Layout.fillWidth: true
            placeholderText: "Server name"
            background: Rectangle { color: "#1e1f22"; radius: 4 }
            color: "white"; placeholderTextColor: "#72767d"
        }
        TextField {
            id: descField; Layout.fillWidth: true
            placeholderText: "Description (optional)"
            background: Rectangle { color: "#1e1f22"; radius: 4 }
            color: "white"; placeholderTextColor: "#72767d"
        }
    }

    footer: DialogButtonBox {
        Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button {
            text: "Create"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: nameField.text.trim() !== ""
        }
    }
    onAccepted: { appState.createServer(nameField.text.trim(), descField.text.trim()); nameField.text = ""; descField.text = "" }
}
