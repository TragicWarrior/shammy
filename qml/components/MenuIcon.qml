import QtQuick

Item {
    id: root
    property string kind: "pin"
    property color stroke: Theme.text
    width: 16
    height: 16

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = root.stroke
            ctx.fillStyle = "transparent"
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const k = root.kind
            if (k === "pin") {
                ctx.beginPath()
                ctx.arc(8, 6.2, 3.1, 0, Math.PI * 2)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(8, 9.4)
                ctx.lineTo(8, 14.2)
                ctx.stroke()
            } else if (k === "folder") {
                ctx.beginPath()
                ctx.moveTo(1.8, 5.2)
                ctx.lineTo(1.8, 3.8)
                ctx.lineTo(6.2, 3.8)
                ctx.lineTo(7.4, 5.4)
                ctx.lineTo(14.2, 5.4)
                ctx.lineTo(14.2, 13.2)
                ctx.lineTo(1.8, 13.2)
                ctx.closePath()
                ctx.stroke()
            } else if (k === "pencil") {
                ctx.beginPath()
                ctx.moveTo(3.2, 13.2)
                ctx.lineTo(3.2, 11.4)
                ctx.lineTo(10.6, 4.0)
                ctx.lineTo(13.0, 6.4)
                ctx.lineTo(5.6, 13.8)
                ctx.lineTo(3.8, 13.8)
                ctx.closePath()
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(9.6, 5.0)
                ctx.lineTo(12.0, 7.4)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(3.2, 11.4)
                ctx.lineTo(5.6, 13.8)
                ctx.stroke()
            } else if (k === "clip") {
                ctx.beginPath()
                ctx.moveTo(10.6, 6.2)
                ctx.lineTo(6.2, 10.6)
                ctx.arc(5.2, 9.6, 1.4, 0.7, Math.PI + 0.9, false)
                ctx.lineTo(9.8, 4.2)
                ctx.arc(11.0, 5.4, 1.6, -2.3, 0.7, false)
                ctx.lineTo(6.4, 11.6)
                ctx.stroke()
            } else if (k === "globe") {
                ctx.beginPath()
                ctx.arc(8, 8, 5.6, 0, Math.PI * 2)
                ctx.stroke()
                ctx.save()
                ctx.translate(8, 8)
                ctx.scale(0.42, 1)
                ctx.beginPath()
                ctx.arc(0, 0, 5.6, 0, Math.PI * 2)
                ctx.restore()
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(2.6, 8)
                ctx.lineTo(13.4, 8)
                ctx.stroke()
            } else if (k === "reply") {
                ctx.beginPath()
                ctx.moveTo(3.0, 7.4)
                ctx.lineTo(7.2, 3.6)
                ctx.moveTo(3.0, 7.4)
                ctx.lineTo(7.2, 11.2)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(7.0, 7.4)
                ctx.lineTo(11.0, 7.4)
                ctx.quadraticCurveTo(13.6, 7.4, 13.6, 10.4)
                ctx.lineTo(13.6, 13.0)
                ctx.stroke()
            } else if (k === "ghost") {
                ctx.beginPath()
                ctx.moveTo(3.2, 8.0)
                ctx.arc(8, 7.2, 4.8, Math.PI, 0, false)
                ctx.lineTo(12.8, 13.8)
                ctx.quadraticCurveTo(11.2, 12.2, 9.6, 13.8)
                ctx.quadraticCurveTo(8.0, 12.2, 6.4, 13.8)
                ctx.quadraticCurveTo(4.8, 12.2, 3.2, 13.8)
                ctx.closePath()
                ctx.stroke()
                ctx.fillStyle = root.stroke
                ctx.beginPath()
                ctx.arc(6.3, 7.3, 0.85, 0, Math.PI * 2)
                ctx.fill()
                ctx.beginPath()
                ctx.arc(9.7, 7.3, 0.85, 0, Math.PI * 2)
                ctx.fill()
            } else if (k === "trash") {
                ctx.beginPath()
                ctx.moveTo(3, 4.6)
                ctx.lineTo(13, 4.6)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(6.4, 4.6)
                ctx.lineTo(6.4, 3.2)
                ctx.lineTo(9.6, 3.2)
                ctx.lineTo(9.6, 4.6)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(4.2, 4.6)
                ctx.lineTo(5.1, 13.4)
                ctx.lineTo(10.9, 13.4)
                ctx.lineTo(11.8, 4.6)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(7, 6.6)
                ctx.lineTo(7.3, 11.4)
                ctx.moveTo(9, 6.6)
                ctx.lineTo(8.7, 11.4)
                ctx.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onKindChanged() { canvas.requestPaint() }
            function onStrokeChanged() { canvas.requestPaint() }
        }
        Component.onCompleted: requestPaint()
    }
}
