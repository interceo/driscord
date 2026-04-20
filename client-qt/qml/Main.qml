import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1100
    height: 700
    minimumWidth: 800
    minimumHeight: 500
    title: "Driscord"

    color: "#1e1f22"

    property string authError: ""

    Component.onCompleted: authManager.tryRestoreSession()

    Connections {
        target: authManager
        function onSessionRestored()     { loader.state = "main" }
        function onSessionRestoreFailed(){ loader.state = "login" }
        function onAuthChanged() {
            root.authError = ""
            if (authManager.loggedIn) loader.state = "main"
            else                      loader.state = "login"
        }
        function onLoginError(message) { root.authError = message }
    }

    Loader {
        id: loader
        anchors.fill: parent
        state: "restoring"

        states: [
            State { name: "restoring"; PropertyChanges { loader.source: "" } },
            State { name: "login";    PropertyChanges { loader.source: "screens/LoginScreen.qml" } },
            State { name: "main";     PropertyChanges { loader.source: "screens/MainScreen.qml" } }
        ]
    }

    // Restoring spinner
    BusyIndicator {
        anchors.centerIn: parent
        visible: loader.state === "restoring"
        running: visible
    }

    // Global error banner
    Rectangle {
        id: errorBanner
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: visible ? 36 : 0
        color: "#ed4245"
        visible: appState.apiError !== "" || root.authError !== ""
        z: 100

        Text {
            anchors.centerIn: parent
            text: appState.apiError !== "" ? appState.apiError : root.authError
            color: "white"
            font.pixelSize: 13
        }
        MouseArea {
            anchors.fill: parent
            onClicked: { appState.apiError = ""; root.authError = "" }
        }
    }
}
