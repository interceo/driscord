import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

DiscordDialog {
    id: root
    title: qsTr("About voice call")
    width: 360

    Connections {
        target: appState
        function onConnectionStatsChanged() { rttCanvas.requestPaint() }
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "transparent"
            Text {
                anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 4 }
                text: qsTr("Connection")
                color: "#5865f2"
                font { pixelSize: 13; bold: true }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 2; color: "#5865f2"; width: 90
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: "#1e1f22"; radius: 4

            Canvas {
                id: rttCanvas
                anchors { fill: parent; margins: 8 }
                antialiasing: true

                property real maxRtt: 50

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()

                    var hist = appState.rttHistory
                    var W = width, H = height

                    var localMax = 50
                    for (var i = 0; i < hist.length; i++) {
                        var v = hist[i].rtt
                        if (v > localMax) localMax = v
                    }
                    var step = 20
                    while (step < localMax) step += 20
                    rttCanvas.maxRtt = step

                    ctx.strokeStyle = "#2c2d31"
                    ctx.fillStyle   = "#72767d"
                    ctx.font        = "9px sans-serif"
                    ctx.lineWidth   = 1
                    for (var g = 0; g <= 2; g++) {
                        var y = H - (g / 2) * H
                        ctx.beginPath()
                        ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke()
                        ctx.fillText(Math.round((g / 2) * step), W - 22, y + 8)
                    }

                    if (hist.length < 2) return

                    ctx.strokeStyle = "#5865f2"
                    ctx.lineWidth   = 2
                    ctx.beginPath()
                    for (var k = 0; k < hist.length; k++) {
                        var x = (k / (hist.length - 1)) * W
                        var rtt = Math.max(0, hist[k].rtt)
                        var yy = H - (rtt / step) * H
                        if (k === 0) ctx.moveTo(x, yy)
                        else         ctx.lineTo(x, yy)
                    }
                    ctx.stroke()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: qsTr("Voice server (SFU)")
                color: "white"; font { pixelSize: 12; bold: true }
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Average latency:"); color: "#b9bbbe"; font.pixelSize: 12 }
                Text {
                    text: appState.avgRttMs >= 0 ? appState.avgRttMs + qsTr(" ms") : "—"
                    color: "white"; font { pixelSize: 12; bold: true }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Last latency:"); color: "#b9bbbe"; font.pixelSize: 12 }
                Text {
                    text: appState.lastRttMs >= 0 ? appState.lastRttMs + qsTr(" ms") : "—"
                    color: "white"; font { pixelSize: 12; bold: true }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Incoming voices:"); color: "#b9bbbe"; font.pixelSize: 12 }
                Text {
                    text: appState.remoteVoiceTracks
                    color: "white"; font { pixelSize: 12; bold: true }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Packets lost:"); color: "#b9bbbe"; font.pixelSize: 12 }
                Text {
                    text: appState.packetsLost
                    color: appState.packetsLost > 0 ? "#faa61a" : "white"
                    font { pixelSize: 12; bold: true }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("If latency exceeds 250 ms the audio may stutter. If issues persist, disconnect and try again.")
            color: "#72767d"; font.pixelSize: 11
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Encrypted in transit (DTLS-SRTP) between you and the server")
            color: "#3ba55d"; font.pixelSize: 11
        }
    }

    footer: DialogButtonBox {
        leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
        background: Rectangle { color: "#2b2d31"; radius: 8 }
        DiscordButton {
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
