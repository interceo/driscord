import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Dialog {
    id: root
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

        // Nav
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

                // Logout — same hover treatment, red text.
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

            // Hidden ListView just for currentIndex tracking
            ListView { id: navList; model: 3; visible: false; currentIndex: 0 }
        }

        // Content
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: navList.currentIndex

            // My Account — Discord-styled card with banner, profile header, and edit rows.
            ScrollView {
                id: myAccountScroll
                clip: true
                contentWidth: availableWidth
                background: Rectangle { color: "#313338" }
                ColumnLayout {
                    x: 16; width: myAccountScroll.availableWidth - 32; spacing: 0
                    Item { implicitHeight: 16 }

                    // ---- Outer card with banner + body ----
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: "#1e1f22"
                        implicitHeight: cardCol.implicitHeight

                        ColumnLayout {
                            id: cardCol
                            anchors.fill: parent
                            spacing: 0

                            // Banner strip
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 80
                                color: "#5865f2"
                                radius: 8
                                // Square the bottom corners visually so the banner attaches to the body.
                                Rectangle {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height: parent.radius
                                    color: parent.color
                                }
                            }

                            // Profile header: avatar overlaps the banner, name on the right.
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 72

                                // Avatar (clickable to change), overlapping the banner above.
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

                            // Inner rows card.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.bottomMargin: 16
                                Layout.topMargin: 8
                                radius: 8
                                color: "#2b2d31"
                                // +32 covers the inner ColumnLayout's top+bottom margins.
                                implicitHeight: rowsCol.implicitHeight + 32

                                ColumnLayout {
                                    id: rowsCol
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12

                                    // ----- Display name row -----
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
                                            // Inline editor
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
                                                // Save
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
                                                // Cancel
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

                                        // Change button — hidden while editing.
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

                                    // ----- Username row (read-only for now) -----
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

                                    // ----- Email row -----
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

            // Audio — card-styled to match My Account.
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

                            // Devices: Input + Output side-by-side via a 2-column grid.
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
                                    model: bridge.listInputDevices()
                                    background: Rectangle { color: "#1e1f22"; radius: 4; border.color: "#1e1f22" }
                                    contentItem: Text {
                                        leftPadding: 10; rightPadding: 30
                                        text: parent.displayText; color: "white"
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
                                        width: inputDevBox.width
                                        contentItem: Text {
                                            text: modelData; color: "white"
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: highlighted ? "#35373c" : "transparent"
                                        }
                                    }
                                }
                                ComboBox {
                                    id: outputDevBox
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: bridge.listOutputDevices()
                                    background: Rectangle { color: "#1e1f22"; radius: 4; border.color: "#1e1f22" }
                                    contentItem: Text {
                                        leftPadding: 10; rightPadding: 30
                                        text: parent.displayText; color: "white"
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
                                        width: outputDevBox.width
                                        contentItem: Text {
                                            text: modelData; color: "white"
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: highlighted ? "#35373c" : "transparent"
                                        }
                                    }
                                }
                            }

                            // Master volume — full-width below the device row.
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

                    Text {
                        text: qsTr("Microphone Processing")
                        color: "white"; font { pixelSize: 16; bold: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: "#2b2d31"
                        implicitHeight: micProcCard.implicitHeight + 32

                        ColumnLayout {
                            id: micProcCard
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                            spacing: 16

                            // ---- Noise gate (legacy RMS, functional today) ----
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("Noise gate")
                                        color: "white"; font { pixelSize: 12; bold: true }
                                    }
                                    Item { Layout.fillWidth: true }
                                    Switch {
                                        id: nsSwitch
                                        checked: bridge.noiseSuppressionEnabled()
                                        onToggled: bridge.setNoiseSuppressionEnabled(checked)
                                    }
                                }
                                Text {
                                    text: qsTr("Drops mic frames when the input RMS is below the threshold. Useful as a quick stopgap until RNNoise lands.")
                                    color: "#b5bac1"; font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    enabled: nsSwitch.checked
                                    Text {
                                        text: qsTr("Threshold")
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 80
                                    }
                                    Slider {
                                        id: gateSlider
                                        from: 0; to: 0.05; stepSize: 0.001
                                        value: bridge.noiseGate()
                                        Layout.fillWidth: true
                                        onValueChanged: bridge.setNoiseGate(value)
                                        background: Rectangle {
                                            x: gateSlider.leftPadding
                                            y: gateSlider.topPadding + gateSlider.availableHeight / 2 - height / 2
                                            width: gateSlider.availableWidth
                                            height: 4; radius: 2
                                            color: "#1e1f22"
                                            Rectangle {
                                                width: gateSlider.visualPosition * parent.width
                                                height: parent.height; radius: 2
                                                color: gateSlider.enabled ? "#5865f2" : "#404249"
                                            }
                                        }
                                        handle: Rectangle {
                                            x: gateSlider.leftPadding + gateSlider.visualPosition * (gateSlider.availableWidth - width)
                                            y: gateSlider.topPadding + gateSlider.availableHeight / 2 - height / 2
                                            width: 16; height: 16; radius: 8
                                            color: "white"
                                            border.color: "#5865f2"; border.width: gateSlider.pressed ? 2 : 0
                                        }
                                    }
                                    Text {
                                        text: gateSlider.value.toFixed(3)
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 50
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: "#3f4147" }

                            // ---- RNNoise (coming soon) ----
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: qsTr("Noise suppression (RNNoise)")
                                        color: "white"; font { pixelSize: 12; bold: true }
                                    }
                                    Text {
                                        text: qsTr("Coming soon — ML-based wideband denoiser.")
                                        color: "#b5bac1"; font.pixelSize: 11
                                    }
                                }
                                Switch {
                                    enabled: false
                                    checked: false
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: "#3f4147" }

                            // ---- VAD (coming soon — full UI scaffold) ----
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                enabled: false

                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: qsTr("Voice activity detection")
                                            color: "white"; font { pixelSize: 12; bold: true }
                                        }
                                        Text {
                                            text: qsTr("Coming soon — drops non-speech frames using VAD probability.")
                                            color: "#b5bac1"; font.pixelSize: 11
                                        }
                                    }
                                    Switch {
                                        id: vadSwitch
                                        checked: bridge.vadEnabled()
                                        onToggled: bridge.setVadEnabled(checked)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("Open")
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 80
                                    }
                                    Slider {
                                        id: vadOpenSlider
                                        from: 0; to: 1; stepSize: 0.01
                                        value: bridge.vadOpenThreshold()
                                        Layout.fillWidth: true
                                        onValueChanged: bridge.setVadOpenThreshold(value)
                                        background: Rectangle {
                                            x: vadOpenSlider.leftPadding
                                            y: vadOpenSlider.topPadding + vadOpenSlider.availableHeight / 2 - height / 2
                                            width: vadOpenSlider.availableWidth
                                            height: 4; radius: 2
                                            color: "#1e1f22"
                                            Rectangle {
                                                width: vadOpenSlider.visualPosition * parent.width
                                                height: parent.height; radius: 2
                                                color: "#5865f2"
                                            }
                                        }
                                        handle: Rectangle {
                                            x: vadOpenSlider.leftPadding + vadOpenSlider.visualPosition * (vadOpenSlider.availableWidth - width)
                                            y: vadOpenSlider.topPadding + vadOpenSlider.availableHeight / 2 - height / 2
                                            width: 16; height: 16; radius: 8
                                            color: "white"
                                            border.color: "#5865f2"; border.width: vadOpenSlider.pressed ? 2 : 0
                                        }
                                    }
                                    Text {
                                        text: vadOpenSlider.value.toFixed(2)
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 40
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("Close")
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 80
                                    }
                                    Slider {
                                        id: vadCloseSlider
                                        from: 0; to: 1; stepSize: 0.01
                                        value: bridge.vadCloseThreshold()
                                        Layout.fillWidth: true
                                        onValueChanged: bridge.setVadCloseThreshold(value)
                                        background: Rectangle {
                                            x: vadCloseSlider.leftPadding
                                            y: vadCloseSlider.topPadding + vadCloseSlider.availableHeight / 2 - height / 2
                                            width: vadCloseSlider.availableWidth
                                            height: 4; radius: 2
                                            color: "#1e1f22"
                                            Rectangle {
                                                width: vadCloseSlider.visualPosition * parent.width
                                                height: parent.height; radius: 2
                                                color: "#5865f2"
                                            }
                                        }
                                        handle: Rectangle {
                                            x: vadCloseSlider.leftPadding + vadCloseSlider.visualPosition * (vadCloseSlider.availableWidth - width)
                                            y: vadCloseSlider.topPadding + vadCloseSlider.availableHeight / 2 - height / 2
                                            width: 16; height: 16; radius: 8
                                            color: "white"
                                            border.color: "#5865f2"; border.width: vadCloseSlider.pressed ? 2 : 0
                                        }
                                    }
                                    Text {
                                        text: vadCloseSlider.value.toFixed(2)
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 40
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("Hangover (ms)")
                                        color: "#b5bac1"; font.pixelSize: 12
                                        Layout.preferredWidth: 110
                                    }
                                    SpinBox {
                                        id: vadHangoverSpin
                                        from: 0; to: 1000; stepSize: 10
                                        value: bridge.vadHangoverMs()
                                        onValueChanged: bridge.setVadHangoverMs(value)
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: "#3f4147" }

                            // ---- Expected packet loss (Opus FEC, functional today) ----
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("Expected packet loss")
                                        color: "white"; font { pixelSize: 12; bold: true }
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: lossSlider.value + "%"
                                        color: "#b5bac1"; font.pixelSize: 12
                                    }
                                }
                                Text {
                                    text: qsTr("Tells Opus how much in-band FEC redundancy to allocate. Raise it on lossy networks.")
                                    color: "#b5bac1"; font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Slider {
                                    id: lossSlider
                                    from: 0; to: 30; stepSize: 1
                                    value: bridge.expectedLossPct()
                                    Layout.fillWidth: true
                                    onValueChanged: bridge.setExpectedLossPct(value)
                                    background: Rectangle {
                                        x: lossSlider.leftPadding
                                        y: lossSlider.topPadding + lossSlider.availableHeight / 2 - height / 2
                                        width: lossSlider.availableWidth
                                        height: 4; radius: 2
                                        color: "#1e1f22"
                                        Rectangle {
                                            width: lossSlider.visualPosition * parent.width
                                            height: parent.height; radius: 2
                                            color: "#5865f2"
                                        }
                                    }
                                    handle: Rectangle {
                                        x: lossSlider.leftPadding + lossSlider.visualPosition * (lossSlider.availableWidth - width)
                                        y: lossSlider.topPadding + lossSlider.availableHeight / 2 - height / 2
                                        width: 16; height: 16; radius: 8
                                        color: "white"
                                        border.color: "#5865f2"; border.width: lossSlider.pressed ? 2 : 0
                                    }
                                }
                            }
                        }
                    }

                    // Primary Apply button (Discord blue)
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
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
                                    bridge.setInputDevice(inputDevBox.currentText)
                                    bridge.setOutputDevice(outputDevBox.currentText)
                                }
                            }
                        }
                    }

                    Item { implicitHeight: 16 }
                }
            }

            // Advanced — card-styled placeholder.
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
                        Layout.preferredHeight: 64
                        radius: 8
                        color: "#2b2d31"
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("No advanced settings yet.")
                            color: "#b5bac1"; font.pixelSize: 13
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
