import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Dialog {
    id: root
    property var inputDeviceModel: []
    property var outputDeviceModel: []
    property string audioDeviceStatus: ""

    function deviceIndex(devices, id) {
        for (let i = 0; i < devices.length; ++i) {
            if (devices[i].id === id)
                return i
        }
        return devices.length > 0 ? 0 : -1
    }

    function refreshAudioDevices() {
        inputDeviceModel = bridge.listInputDevices()
        outputDeviceModel = bridge.listOutputDevices()
        inputDevBox.currentIndex = deviceIndex(
            inputDeviceModel, bridge.currentInputDevice())
        outputDevBox.currentIndex = deviceIndex(
            outputDeviceModel, bridge.currentOutputDevice())
    }

    function openAdvanced() {
        navList.currentIndex = 2
        open()
    }

    onOpened: {
        audioDeviceStatus = ""
        refreshAudioDevices()
    }
    title: "Settings"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 920; height: 640
    background: Rectangle { color: "#313338" }
    header: Item {
        height: 42
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 16 }
            text: "Settings"; color: "white"; font { pixelSize: 15; bold: true }
        }
    }

    AvatarCropDialog { id: avatarCropDlg }

    RowLayout {
        anchors.fill: parent; spacing: 0

        Rectangle {
            Layout.preferredWidth: 180; Layout.fillHeight: true
            color: "#2b2d31"

            ColumnLayout {
                anchors { fill: parent; topMargin: 16 }
                spacing: 2

                Repeater {
                    model: ["My Account", "Audio", "Advanced"]
                    delegate: Rectangle {
                        id: navItem
                        Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                        height: 32; radius: 4
                        property bool selected: navList.currentIndex === index
                        color: selected ? "#404249"
                             : navMouse.containsMouse ? "#35373c"
                             : "transparent"
                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 12 }
                            text: modelData
                            color: navItem.selected ? "white" : "#b5bac1"
                            font.pixelSize: 14
                        }
                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: navList.currentIndex = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    id: logoutItem
                    Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                    Layout.bottomMargin: 8
                    height: 32; radius: 4
                    color: logoutMouse.containsMouse ? "#35373c" : "transparent"
                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 12 }
                        text: "Log Out"; color: "#ed4245"; font.pixelSize: 14
                    }
                    MouseArea {
                        id: logoutMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.close(); authManager.logout() }
                    }
                }
            }

            ListView { id: navList; model: 3; visible: false; currentIndex: 0 }
        }

        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: navList.currentIndex

            ScrollView {
                id: myAccountScroll
                clip: true
                contentWidth: availableWidth
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 16; width: myAccountScroll.availableWidth - 32; spacing: 0
                    Item { implicitHeight: 16 }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: "#1e1f22"
                        implicitHeight: cardCol.implicitHeight

                        ColumnLayout {
                            id: cardCol
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 80
                                color: "#5865f2"
                                radius: 8
                                Rectangle {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height: parent.radius
                                    color: parent.color
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 72

                                Rectangle {
                                    id: avatarHolder
                                    width: 88; height: 88
                                    radius: width / 2
                                    color: "#1e1f22"
                                    x: 16
                                    y: -44

                                    AvatarBox {
                                        anchors.centerIn: parent
                                        size: 80
                                        displayName: appState.userProfile.displayName || authManager.username
                                        avatarUrl: appState.userProfile.avatarUrl ?? ""
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            avatarCropDlg.initialUrl = appState.userProfile.avatarUrl ?? ""
                                            avatarCropDlg.open()
                                        }
                                    }
                                }

                                ColumnLayout {
                                    anchors {
                                        left: avatarHolder.right; leftMargin: 16
                                        verticalCenter: parent.verticalCenter
                                    }
                                    spacing: 2
                                    Text {
                                        text: appState.userProfile.displayName || authManager.username
                                        color: "white"; font { pixelSize: 18; bold: true }
                                    }
                                    Text {
                                        text: "@" + (authManager.username ?? "")
                                        color: "#b5bac1"; font.pixelSize: 13
                                        visible: (appState.userProfile.displayName ?? "") !== ""
                                                 && appState.userProfile.displayName !== authManager.username
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.bottomMargin: 16
                                Layout.topMargin: 8
                                radius: 8
                                color: "#2b2d31"
                                implicitHeight: rowsCol.implicitHeight + 32

                                ColumnLayout {
                                    id: rowsCol
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                text: qsTr("Display name")
                                                color: "white"; font { pixelSize: 12; bold: true }
                                            }
                                            Text {
                                                text: (appState.userProfile.displayName && appState.userProfile.displayName.length > 0)
                                                      ? appState.userProfile.displayName
                                                      : qsTr("You haven't set a display name yet.")
                                                color: "#b5bac1"; font.pixelSize: 13
                                                visible: !displayNameEditor.editing
                                            }
                                            RowLayout {
                                                id: displayNameEditor
                                                property bool editing: false
                                                Layout.fillWidth: true
                                                spacing: 8
                                                visible: editing

                                                TextField {
                                                    id: displayNameField
                                                    Layout.fillWidth: true
                                                    placeholderText: qsTr("Display name")
                                                    background: Rectangle { color: "#1e1f22"; radius: 4 }
                                                    color: "white"; placeholderTextColor: "#72767d"
                                                    leftPadding: 10
                                                }
                                                Rectangle {
                                                    radius: 4
                                                    color: saveArea.containsMouse ? "#4752c4" : "#5865f2"
                                                    Layout.preferredHeight: 32
                                                    Layout.preferredWidth: saveText.implicitWidth + 24
                                                    Text {
                                                        id: saveText
                                                        anchors.centerIn: parent
                                                        text: qsTr("Save"); color: "white"
                                                        font { pixelSize: 13; bold: true }
                                                    }
                                                    MouseArea {
                                                        id: saveArea
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            appState.updateDisplayName(displayNameField.text)
                                                            displayNameEditor.editing = false
                                                        }
                                                    }
                                                }
                                                Rectangle {
                                                    radius: 4
                                                    color: cancelArea.containsMouse ? "#4e5058" : "transparent"
                                                    Layout.preferredHeight: 32
                                                    Layout.preferredWidth: cancelText.implicitWidth + 24
                                                    Text {
                                                        id: cancelText
                                                        anchors.centerIn: parent
                                                        text: qsTr("Cancel"); color: "#dbdee1"
                                                        font.pixelSize: 13
                                                    }
                                                    MouseArea {
                                                        id: cancelArea
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: displayNameEditor.editing = false
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            visible: !displayNameEditor.editing
                                            Layout.preferredHeight: 32
                                            Layout.preferredWidth: changeText.implicitWidth + 24
                                            radius: 4
                                            color: changeArea.containsMouse ? "#6d6f78" : "#4e5058"
                                            Text {
                                                id: changeText
                                                anchors.centerIn: parent
                                                text: qsTr("Change"); color: "white"
                                                font.pixelSize: 13
                                            }
                                            MouseArea {
                                                id: changeArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    displayNameField.text = appState.userProfile.displayName ?? ""
                                                    displayNameEditor.editing = true
                                                    displayNameField.forceActiveFocus()
                                                }
                                            }
                                        }
                                    }

                                    Rectangle { Layout.fillWidth: true; height: 1; color: "#3f4147" }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                text: qsTr("Username")
                                                color: "white"; font { pixelSize: 12; bold: true }
                                            }
                                            Text {
                                                text: authManager.username ?? ""
                                                color: "#b5bac1"; font.pixelSize: 13
                                            }
                                        }
                                    }

                                    Rectangle { Layout.fillWidth: true; height: 1; color: "#3f4147" }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                text: qsTr("Email")
                                                color: "white"; font { pixelSize: 12; bold: true }
                                            }
                                            RowLayout {
                                                id: emailRow
                                                spacing: 6
                                                property string email: appState.userProfile.email ?? ""
                                                property bool revealed: false
                                                function maskEmail(e) {
                                                    var at = e.indexOf("@")
                                                    if (at <= 0) return e
                                                    return "*".repeat(at) + e.substring(at)
                                                }
                                                Text {
                                                    text: emailRow.email.length > 0
                                                          ? (emailRow.revealed ? emailRow.email
                                                                               : emailRow.maskEmail(emailRow.email))
                                                          : qsTr("No email on file.")
                                                    color: "#b5bac1"; font.pixelSize: 13
                                                }
                                                Text {
                                                    visible: emailRow.email.length > 0
                                                    text: emailRow.revealed ? qsTr("Hide") : qsTr("Show")
                                                    color: emailRevealArea.containsMouse ? "#a8c0ff" : "#5865f2"
                                                    font { pixelSize: 13; underline: emailRevealArea.containsMouse }
                                                    MouseArea {
                                                        id: emailRevealArea
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: emailRow.revealed = !emailRow.revealed
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item { implicitHeight: 16 }
                }
            }

            ScrollView {
                id: audioScroll
                clip: true
                contentWidth: availableWidth
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 16; width: audioScroll.availableWidth - 32; spacing: 16
                    Item { implicitHeight: 16 }

                    Text {
                        text: qsTr("Voice Settings")
                        color: "white"; font { pixelSize: 16; bold: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: "#2b2d31"
                        implicitHeight: voiceCard.implicitHeight + 32

                        ColumnLayout {
                            id: voiceCard
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                            spacing: 16

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Input device")
                                    color: "white"; font { pixelSize: 12; bold: true }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Output device")
                                    color: "white"; font { pixelSize: 12; bold: true }
                                    elide: Text.ElideRight
                                }

                                ComboBox {
                                    id: inputDevBox
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: root.inputDeviceModel
                                    textRole: "name"
                                    valueRole: "id"
                                    background: Rectangle { color: "#1e1f22"; radius: 4; border.color: "#1e1f22" }
                                    contentItem: Text {
                                        leftPadding: 10; rightPadding: 30
                                        text: inputDevBox.displayText; color: "white"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                    popup: Popup {
                                        y: parent.height
                                        width: parent.width
                                        padding: 1
                                        background: Rectangle { color: "#1e1f22"; radius: 4; border.color: "#111214" }
                                        contentItem: ListView {
                                            implicitHeight: contentHeight
                                            model: inputDevBox.popup.visible ? inputDevBox.delegateModel : null
                                            currentIndex: inputDevBox.highlightedIndex
                                            clip: true
                                        }
                                    }
                                    delegate: ItemDelegate {
                                        id: inputDeviceDelegate
                                        required property var modelData
                                        width: inputDevBox.width
                                        contentItem: Text {
                                            text: inputDeviceDelegate.modelData.name
                                            color: "white"
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: inputDeviceDelegate.highlighted
                                                ? "#35373c" : "transparent"
                                        }
                                    }
                                }
                                ComboBox {
                                    id: outputDevBox
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: root.outputDeviceModel
                                    textRole: "name"
                                    valueRole: "id"
                                    background: Rectangle { color: "#1e1f22"; radius: 4; border.color: "#1e1f22" }
                                    contentItem: Text {
                                        leftPadding: 10; rightPadding: 30
                                        text: outputDevBox.displayText; color: "white"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                    popup: Popup {
                                        y: parent.height
                                        width: parent.width
                                        padding: 1
                                        background: Rectangle { color: "#1e1f22"; radius: 4; border.color: "#111214" }
                                        contentItem: ListView {
                                            implicitHeight: contentHeight
                                            model: outputDevBox.popup.visible ? outputDevBox.delegateModel : null
                                            currentIndex: outputDevBox.highlightedIndex
                                            clip: true
                                        }
                                    }
                                    delegate: ItemDelegate {
                                        id: outputDeviceDelegate
                                        required property var modelData
                                        width: outputDevBox.width
                                        contentItem: Text {
                                            text: outputDeviceDelegate.modelData.name
                                            color: "white"
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: outputDeviceDelegate.highlighted
                                                ? "#35373c" : "transparent"
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("Master volume")
                                        color: "white"; font { pixelSize: 12; bold: true }
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: Math.round(volSlider.value * 100) + "%"
                                        color: "#b5bac1"; font.pixelSize: 12
                                    }
                                }
                                Slider {
                                    id: volSlider
                                    from: 0; to: 2; value: 1.0
                                    Layout.fillWidth: true
                                    onValueChanged: bridge.setMasterVolume(value)
                                    background: Rectangle {
                                        x: volSlider.leftPadding
                                        y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                                        width: volSlider.availableWidth
                                        height: 4; radius: 2
                                        color: "#1e1f22"
                                        Rectangle {
                                            width: volSlider.visualPosition * parent.width
                                            height: parent.height; radius: 2
                                            color: "#5865f2"
                                        }
                                    }
                                    handle: Rectangle {
                                        x: volSlider.leftPadding + volSlider.visualPosition * (volSlider.availableWidth - width)
                                        y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                                        width: 16; height: 16; radius: 8
                                        color: "white"
                                        border.color: "#5865f2"; border.width: volSlider.pressed ? 2 : 0
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.audioDeviceStatus
                            color: root.audioDeviceStatus === qsTr("Audio devices updated")
                                ? "#23a559" : "#f23f42"
                            font.pixelSize: 12
                            visible: text.length > 0
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            id: applyBtn
                            Layout.preferredHeight: 36
                            Layout.preferredWidth: applyText.implicitWidth + 32
                            radius: 4
                            color: applyArea.containsMouse ? "#4752c4" : "#5865f2"
                            Text {
                                id: applyText
                                anchors.centerIn: parent
                                text: qsTr("Apply"); color: "white"
                                font { pixelSize: 13; bold: true }
                            }
                            MouseArea {
                                id: applyArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const inputOk = inputDevBox.currentIndex >= 0
                                        && bridge.setInputDevice(inputDevBox.currentValue)
                                    const outputOk = outputDevBox.currentIndex >= 0
                                        && bridge.setOutputDevice(outputDevBox.currentValue)
                                    root.audioDeviceStatus = inputOk && outputOk
                                        ? qsTr("Audio devices updated")
                                        : qsTr("Could not activate one of the devices")
                                    root.refreshAudioDevices()
                                }
                            }
                        }
                    }

                    Item { implicitHeight: 16 }
                }
            }

            ScrollView {
                id: advancedScroll
                clip: true
                contentWidth: availableWidth
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 16; width: advancedScroll.availableWidth - 32; spacing: 16
                    Item { implicitHeight: 16 }

                    Text {
                        text: qsTr("Advanced")
                        color: "white"; font { pixelSize: 16; bold: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: updateColumn.implicitHeight + 32
                        radius: 8
                        color: "#2b2d31"

                        ColumnLayout {
                            id: updateColumn
                            anchors { fill: parent; margins: 16 }
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTr("Updates")
                                    color: "white"
                                    font { pixelSize: 14; bold: true }
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: qsTr("Version %1")
                                        .arg(updateManager.currentVersion)
                                    color: "#949ba4"; font.pixelSize: 12
                                }
                            }

                            Text {
                                id: updateStatus
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: 13
                                visible: text.length > 0
                                color: {
                                    switch (updateManager.state) {
                                    case "error": return "#f23f42"
                                    case "upToDate":
                                    case "updateAvailable":
                                    case "readyToApply": return "#23a559"
                                    default: return "#b5bac1"
                                    }
                                }
                                text: {
                                    switch (updateManager.state) {
                                    case "disabled":
                                        return qsTr("Updates are disabled in this build.")
                                    case "checking":
                                        return qsTr("Checking for updates…")
                                    case "upToDate":
                                        return qsTr("You are up to date.")
                                    case "updateAvailable":
                                        return qsTr("Version %1 is available.")
                                            .arg(updateManager.latestVersion)
                                    case "downloading":
                                        return qsTr("Downloading %1… %2%")
                                            .arg(updateManager.latestVersion)
                                            .arg(Math.round(updateManager.downloadProgress * 100))
                                    case "verifying":
                                        return qsTr("Verifying the download…")
                                    case "extracting":
                                        return qsTr("Unpacking the update…")
                                    case "readyToApply":
                                        return updateManager.canApply
                                            ? qsTr("Version %1 is ready to install.")
                                                .arg(updateManager.latestVersion)
                                            : updateManager.applyHint
                                    case "applying":
                                        return qsTr("Installing…")
                                    case "error":
                                        return updateManager.errorText
                                    default:
                                        return ""
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 4
                                radius: 2
                                color: "#1e1f22"
                                visible: updateManager.state === "downloading"
                                Rectangle {
                                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                    width: parent.width * updateManager.downloadProgress
                                    radius: 2
                                    color: "#5865f2"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                Rectangle {
                                    id: updateBtn
                                    visible: updateManager.state !== "disabled"
                                    property bool busy: ["checking", "downloading",
                                        "verifying", "extracting", "applying"]
                                        .indexOf(updateManager.state) >= 0
                                    property string label: {
                                        if (updateManager.state === "updateAvailable")
                                            return qsTr("Download update %1")
                                                .arg(updateManager.latestVersion)
                                        if (updateManager.state === "readyToApply"
                                                && updateManager.canApply)
                                            return qsTr("Restart && install")
                                        return qsTr("Check for updates")
                                    }
                                    Layout.preferredHeight: 36
                                    Layout.preferredWidth: updateBtnText.implicitWidth + 32
                                    radius: 4
                                    opacity: busy ? 0.5 : 1.0
                                    color: updateBtnArea.containsMouse && !busy
                                        ? "#4752c4" : "#5865f2"
                                    Text {
                                        id: updateBtnText
                                        anchors.centerIn: parent
                                        text: updateBtn.label
                                        color: "white"
                                        font { pixelSize: 13; bold: true }
                                    }
                                    MouseArea {
                                        id: updateBtnArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: updateBtn.busy
                                            ? Qt.ArrowCursor : Qt.PointingHandCursor
                                        onClicked: {
                                            if (updateBtn.busy)
                                                return
                                            if (updateManager.state === "updateAvailable")
                                                updateManager.downloadUpdate()
                                            else if (updateManager.state === "readyToApply"
                                                     && updateManager.canApply)
                                                updateManager.applyAndRestart()
                                            else
                                                updateManager.checkForUpdates()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item { implicitHeight: 16 }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 56
        color: "#2b2d31"

        Rectangle {
            id: closeBtn
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 16 }
            width: closeText.implicitWidth + 32
            height: 36
            radius: 4
            color: closeArea.containsMouse ? "#6d6f78" : "#4e5058"
            Text {
                id: closeText
                anchors.centerIn: parent
                text: qsTr("Close"); color: "white"
                font { pixelSize: 13; bold: true }
            }
            MouseArea {
                id: closeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.close()
            }
        }
    }
}
