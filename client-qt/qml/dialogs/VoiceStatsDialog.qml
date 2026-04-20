import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: qsTr("About voice call")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 360
    padding: 16

    background: Rectangle { color: "#2b2d31"; radius: 6 }

    // Re-render the chart whenever stats change
    Connections {
        target: appState
        function onConnectionStatsChanged() { rttCanvas.requestPaint() }
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        // Tab header
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

        // Ping graph
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

                    // Compute scale
                    var localMax = 50
                    for (var i = 0; i < hist.length; i++) {
                        var v = hist[i].rtt
                        if (v > localMax) localMax = v
                    }
                    // Round up to a nice number
                    var step = 20
                    while (step < localMax) step += 20
                    rttCanvas.maxRtt = step

                    // Grid lines + Y labels
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

                    // Line
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

        // Stats text block
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: appState.primaryPeerId !== "" ? appState.primaryPeerId : qsTr("(no peers)")
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
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("If latency exceeds 250 ms the audio may stutter. If issues persist, disconnect and try again.")
            color: "#72767d"; font.pixelSize: 11
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Protected with end-to-end encryption")
            color: "#3ba55d"; font.pixelSize: 11
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
    }
}
