import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

            TextField {
                id: usernameField
                Layout.fillWidth: true
                placeholderText: "Username"
                font.pixelSize: 14
                background: Rectangle { color: "#1e1f22"; radius: 4 }
                color: "white"
                placeholderTextColor: "#72767d"
            }

            TextField {
                id: emailField
                Layout.fillWidth: true
                placeholderText: "Email"
                font.pixelSize: 14
                visible: !root.isLogin
                background: Rectangle { color: "#1e1f22"; radius: 4 }
                color: "white"
                placeholderTextColor: "#72767d"
            }

            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: "Password"
                echoMode: TextInput.Password
                font.pixelSize: 14
                background: Rectangle { color: "#1e1f22"; radius: 4 }
                color: "white"
                placeholderTextColor: "#72767d"
                Keys.onReturnPressed: submitBtn.clicked()
            }

            Button {
                id: submitBtn
                Layout.fillWidth: true
                text: root.isLogin ? "Log In" : "Register"
                font.pixelSize: 15
                topPadding: 10; bottomPadding: 10
                background: Rectangle {
                    radius: 4
                    color: submitBtn.pressed ? "#3e6de8" : "#5865f2"
                }
                contentItem: Text {
                    text: submitBtn.text
                    color: "white"
                    font: submitBtn.font
                    horizontalAlignment: Text.AlignHCenter
                }
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
