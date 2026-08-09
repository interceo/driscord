import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property bool active: true
    property bool compact: true
    property string peerId: ""
    property var stats: ({})
    property var history: []
    property var rates: ({})
    property var previousCounters: ({})
    property double previousAtMs: 0

    onPeerIdChanged: {
        stats = ({})
        history = []
        rates = ({})
        previousCounters = ({})
        previousAtMs = 0
    }

    width: compact ? 232 : 320
    height: compact ? 158 : 190
    radius: 6
    color: "#000000b3"
    border.color: "#ffffff22"
    border.width: 1
    visible: active

    function numberAt(obj, key, fallback) {
        if (!obj || obj[key] === undefined || obj[key] === null) return fallback
        return typeof obj[key] === "number" ? obj[key] : fallback
    }

    function fmtMs(v) {
        return v >= 0 ? (Math.round(v) + " ms") : "--"
    }

    function counterAt(section, key) {
        return root.numberAt(section || {}, key, 0)
    }

    function rateFor(name, current, elapsedSec) {
        if (root.previousCounters[name] === undefined || elapsedSec <= 0) return 0
        return Math.max(0, (current - root.previousCounters[name]) / elapsedSec)
    }

    function fmtRate(v) {
        return v >= 10 ? Math.round(v).toString() : v.toFixed(1)
    }

    function refresh() {
        try {
            var parsed = JSON.parse(bridge.screenStatsJson(root.peerId))
            root.stats = parsed
            var video = parsed.video || {}
            var audio = parsed.audio || {}
            var nowMs = Date.now()
            var elapsedSec = root.previousAtMs > 0 ? (nowMs - root.previousAtMs) / 1000 : 0
            var counters = {
                videoLoss: root.counterAt(video, "packetsLost"),
                videoDrops: root.counterAt(video, "framesDropped"),
                videoDecoded: root.counterAt(video, "framesDecoded"),
                videoKeyframes: root.counterAt(video, "keyFramesDecoded"),
                videoPackets: root.counterAt(video, "packetsReceived"),
                audioLoss: root.counterAt(audio, "packetsLost"),
                audioConceals: root.counterAt(audio, "concealedSamples"),
                audioPackets: root.counterAt(audio, "packetsReceived")
            }
            root.rates = {
                videoLoss: root.rateFor("videoLoss", counters.videoLoss, elapsedSec),
                videoDrops: root.rateFor("videoDrops", counters.videoDrops, elapsedSec),
                videoDecoded: root.rateFor("videoDecoded", counters.videoDecoded, elapsedSec),
                videoKeyframes: root.rateFor("videoKeyframes", counters.videoKeyframes, elapsedSec),
                videoPackets: root.rateFor("videoPackets", counters.videoPackets, elapsedSec),
                audioLoss: root.rateFor("audioLoss", counters.audioLoss, elapsedSec),
                audioConceals: root.rateFor("audioConceals", counters.audioConceals, elapsedSec),
                audioPackets: root.rateFor("audioPackets", counters.audioPackets, elapsedSec)
            }
            root.previousCounters = counters
            root.previousAtMs = nowMs

            var actual = root.numberAt(video, "actualDelayMs", -1)
            var target = root.numberAt(video, "targetDelayMs", -1)
            var audioActual = root.numberAt(audio, "actualDelayMs", -1)
            if (actual >= 0 || target >= 0 || audioActual >= 0) {
                var next = root.history.slice(Math.max(0, root.history.length - 47))
                next.push({ actual: actual, target: target, audioActual: audioActual })
                root.history = next
            }
            graph.requestPaint()
        } catch (e) {
            root.stats = {}
        }
    }

    Timer {
        interval: 500
        running: root.visible && root.active
        repeat: true
        triggeredOnStart: true
        onTriggered: root.refresh()
    }

    ColumnLayout {
        anchors { fill: parent; margins: 8 }
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "STREAM"
                color: "#ffffff"
                font { pixelSize: 10; bold: true }
            }
            Text {
                Layout.fillWidth: true
                text: root.numberAt(root.stats, "measuredKbps", 0) + " kbps"
                color: "#23a55a"
                font { pixelSize: compact ? 9 : 10; bold: true }
                horizontalAlignment: Text.AlignRight
            }
        }

        Canvas {
            id: graph
            Layout.fillWidth: true
            Layout.preferredHeight: compact ? 36 : 52
            antialiasing: true

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()

                var W = width
                var H = height
                ctx.fillStyle = "rgba(0, 0, 0, 0.70)"
                ctx.fillRect(0, 0, W, H)

                var hist = root.history
                var maxValue = 80
                for (var i = 0; i < hist.length; i++) {
                    maxValue = Math.max(maxValue, hist[i].actual, hist[i].target, hist[i].audioActual)
                }
                maxValue = Math.ceil(maxValue / 50) * 50

                ctx.strokeStyle = "#ffffff18"
                ctx.lineWidth = 1
                for (var g = 0; g <= 2; g++) {
                    var gy = H - (g / 2) * H
                    ctx.beginPath()
                    ctx.moveTo(0, gy)
                    ctx.lineTo(W, gy)
                    ctx.stroke()
                }

                function drawLine(key, color) {
                    if (hist.length < 2) return
                    ctx.strokeStyle = color
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    var started = false
                    for (var k = 0; k < hist.length; k++) {
                        var v = hist[k][key]
                        if (v < 0) continue
                        var x = (k / 47) * W
                        var y = H - Math.min(1, v / maxValue) * H
                        if (!started) {
                            ctx.moveTo(x, y)
                            started = true
                        } else {
                            ctx.lineTo(x, y)
                        }
                    }
                    if (started) ctx.stroke()
                }

                drawLine("target", "#5865f2")
                drawLine("actual", "#f0b232")
                drawLine("audioActual", "#23a55a")

                ctx.fillStyle = "#b9bbbe"
                ctx.font = "9px sans-serif"
                ctx.fillText(maxValue + " ms", 4, 10)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 2

            Text {
                text: "V actual / target"
                color: "#b9bbbe"
                font.pixelSize: 10
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: {
                    var v = root.stats.video || {}
                    root.fmtMs(root.numberAt(v, "actualDelayMs", -1)) + " / "
                        + root.fmtMs(root.numberAt(v, "targetDelayMs", -1))
                }
                color: "#ffffff"
                font.pixelSize: 10
            }

            Text {
                text: "A actual / target"
                color: "#b9bbbe"
                font.pixelSize: 10
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: {
                    var a = root.stats.audio || {}
                    root.fmtMs(root.numberAt(a, "actualDelayMs", -1)) + " / "
                        + root.fmtMs(root.numberAt(a, "targetDelayMs", -1))
                }
                color: "#ffffff"
                font.pixelSize: 10
            }

            Text {
                text: "V loss / drop · s⁻¹"
                color: "#b9bbbe"
                font.pixelSize: 10
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: {
                    root.fmtRate(root.numberAt(root.rates, "videoLoss", 0)) + " / "
                        + root.fmtRate(root.numberAt(root.rates, "videoDrops", 0))
                }
                color: "#ffffff"
                font.pixelSize: 10
            }

            Text {
                text: "A conceal / loss · s⁻¹"
                color: "#b9bbbe"
                font.pixelSize: 10
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: {
                    root.fmtRate(root.numberAt(root.rates, "audioConceals", 0)) + " / "
                        + root.fmtRate(root.numberAt(root.rates, "audioLoss", 0))
                }
                color: "#ffffff"
                font.pixelSize: 10
            }

            Text {
                text: "V decoded / keyframe · s⁻¹"
                color: "#b9bbbe"
                font.pixelSize: 10
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: {
                    root.fmtRate(root.numberAt(root.rates, "videoDecoded", 0)) + " / "
                        + root.fmtRate(root.numberAt(root.rates, "videoKeyframes", 0))
                }
                color: "#ffffff"
                font.pixelSize: 10
            }

            Text {
                text: "V / A packets · s⁻¹"
                color: "#b9bbbe"
                font.pixelSize: 10
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: {
                    root.fmtRate(root.numberAt(root.rates, "videoPackets", 0)) + " / "
                        + root.fmtRate(root.numberAt(root.rates, "audioPackets", 0))
                }
                color: "#ffffff"
                font.pixelSize: 10
            }

        }
    }
}
