import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../dialogs"

Item {
    id: root

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ServerColumn {
            id: serverCol
            Layout.preferredWidth: 72
            Layout.fillHeight: true
        }

        Sidebar {
            id: sidebar
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            onSettingsRequested:   settingsDialog.open()
            onShareRequested:      shareDialog.open()
            onVoiceStatsRequested: voiceStatsDialog.open()
            onInviteUsersRequested: inviteUsersDialog.open()
        }

        ContentPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Rectangle {
        id: updateBanner
        objectName: "updateBanner"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: visible ? 36 : 0
        color: "#23a559"
        visible: updateManager.noticeVisible
        z: 100

        Text {
            anchors.centerIn: parent
            text: qsTr("Update %1 is available — click to view")
                .arg(updateManager.latestVersion)
            color: "white"
            font.pixelSize: 13
        }
        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 36
            cursorShape: Qt.PointingHandCursor
            onClicked: settingsDialog.openAdvanced()
        }
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 12 }
            text: "✕"
            color: "white"
            font.pixelSize: 13
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                onClicked: updateManager.dismissNotice()
            }
        }
    }

    SettingsDialog  { id: settingsDialog }
    ShareDialog     { id: shareDialog }
    CreateServerDialog  { id: createServerDialog }
    CreateChannelDialog { id: createChannelDialog }
    JoinByInviteDialog  { id: joinInviteDialog }
    InviteUsersDialog   { id: inviteUsersDialog }
    AvatarCropDialog    { id: avatarCropDialog }
    VoiceStatsDialog    { id: voiceStatsDialog }
}
