import QtQuick

Item {
    id: root
    property string avatarUrl: ""
    property string displayName: ""
    property int size: 32

    width: size; height: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.displayName.length > 0 ? hashColor(root.displayName) : "#5865f2"
        visible: avatarImg.status !== Image.Ready

        Text {
            anchors.centerIn: parent
            text: root.displayName.charAt(0).toUpperCase()
            color: "white"
            font { pixelSize: root.size * 0.42; bold: true }
        }
    }

    Image {
        id: avatarImg
        source: root.avatarUrl
        visible: false
        onStatusChanged: if (status === Image.Ready) avatarCanvas.requestPaint()
    }

    Canvas {
        id: avatarCanvas
        anchors.fill: parent
        visible: avatarImg.status === Image.Ready

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (avatarImg.status !== Image.Ready) return
            var iw = avatarImg.sourceSize.width
            var ih = avatarImg.sourceSize.height
            if (iw <= 0 || ih <= 0) return
            ctx.save()
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, width / 2, 0, Math.PI * 2)
            ctx.clip()
            var scale = Math.max(width / iw, height / ih)
            var sw = iw * scale
            var sh = ih * scale
            ctx.drawImage(avatarImg, (width - sw) / 2, (height - sh) / 2, sw, sh)
            ctx.restore()
        }
    }

    function hashColor(s) {
        var colors = ["#5865f2","#3ba55c","#faa61a","#ed4245","#eb459e","#9b59b6"]
        var h = 0
        for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) & 0xffff
        return colors[h % colors.length]
    }
}
