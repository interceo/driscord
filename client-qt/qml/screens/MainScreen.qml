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
        }

        ContentPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    SettingsDialog  { id: settingsDialog }
    ShareDialog     { id: shareDialog }
    CreateServerDialog  { id: createServerDialog }
    CreateChannelDialog { id: createChannelDialog }
    JoinByInviteDialog  { id: joinInviteDialog }
    AvatarCropDialog    { id: avatarCropDialog }
    VoiceStatsDialog    { id: voiceStatsDialog }
}
