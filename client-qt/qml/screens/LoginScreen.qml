import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    property bool isLogin: true

    Rectangle {
        anchors.centerIn: parent
        width: 380
        height: col.implicitHeight + 48
        radius: 8
        color: "#2b2d31"

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
            spacing: 14

            Text {
                text: isLogin ? "Welcome back!" : "Create an account"
                color: "white"
                font { pixelSize: 22; bold: true }
                Layout.alignment: Qt.AlignHCenter
                topPadding: 8
            }

            TabBar {
                id: tabBar
                Layout.fillWidth: true
                background: Rectangle { color: "transparent" }
                TabButton { text: "Log In";   font.pixelSize: 14 }
                TabButton { text: "Register"; font.pixelSize: 14 }
                onCurrentIndexChanged: root.isLogin = (currentIndex === 0)
            }

            DiscordTextField {
                id: usernameField
                Layout.fillWidth: true
                placeholderText: "Username"
                font.pixelSize: 14
            }

            DiscordTextField {
                id: emailField
                Layout.fillWidth: true
                placeholderText: "Email"
                font.pixelSize: 14
                visible: !root.isLogin
            }

            DiscordTextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: "Password"
                echoMode: TextInput.Password
                font.pixelSize: 14
                Keys.onReturnPressed: submitBtn.clicked()
            }

            DiscordButton {
                id: submitBtn
                Layout.fillWidth: true
                text: root.isLogin ? "Log In" : "Register"
                font.pixelSize: 15
                onClicked: {
                    if (root.isLogin)
                        authManager.login(usernameField.text, passwordField.text)
                    else
                        authManager.registerUser(usernameField.text, emailField.text, passwordField.text)
                }
            }
        }
    }
}
