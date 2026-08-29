import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    objectName: "loginScreen"
    property bool isLogin: true

    function utf8Length(value) {
        return encodeURIComponent(value).replace(/%[0-9A-F]{2}/gi, "x").length
    }

    readonly property string normalizedUsername: usernameField.text.trim()
    readonly property string normalizedEmail: emailField.text.trim()
    readonly property bool validUsername: normalizedUsername.length > 0
                                                 && normalizedUsername.length <= 32
    readonly property bool validPassword: passwordField.text.length >= (isLogin ? 1 : 6)
                                                 && utf8Length(passwordField.text) <= 72
    readonly property bool validEmail: /^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(normalizedEmail)
    readonly property bool canSubmit: validUsername && validPassword
                                          && (isLogin || validEmail)

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
                objectName: "authModeTabs"
                Layout.fillWidth: true
                background: Rectangle { color: "transparent" }
                TabButton { text: "Log In";   font.pixelSize: 14 }
                TabButton { text: "Register"; font.pixelSize: 14 }
                onCurrentIndexChanged: root.isLogin = (currentIndex === 0)
            }

            DiscordTextField {
                id: usernameField
                objectName: "usernameField"
                Layout.fillWidth: true
                placeholderText: "Username"
                font.pixelSize: 14
                maximumLength: 32
            }

            DiscordTextField {
                id: emailField
                objectName: "emailField"
                Layout.fillWidth: true
                placeholderText: "Email"
                font.pixelSize: 14
                visible: !root.isLogin
                maximumLength: 255
            }

            DiscordTextField {
                id: passwordField
                objectName: "passwordField"
                Layout.fillWidth: true
                placeholderText: "Password"
                echoMode: TextInput.Password
                font.pixelSize: 14
                maximumLength: 72
                Keys.onReturnPressed: {
                    if (submitBtn.enabled)
                        submitBtn.clicked()
                }
            }

            DiscordButton {
                id: submitBtn
                objectName: "authSubmitButton"
                Layout.fillWidth: true
                text: authManager.authPending ? "Please wait…"
                                              : (root.isLogin ? "Log In" : "Register")
                font.pixelSize: 15
                enabled: root.canSubmit && !authManager.authPending
                onClicked: {
                    if (root.isLogin)
                        authManager.login(root.normalizedUsername, passwordField.text)
                    else
                        authManager.registerUser(root.normalizedUsername,
                                                 root.normalizedEmail,
                                                 passwordField.text)
                }
            }
        }
    }
}
