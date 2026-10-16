// The handles over the viewport: drag to move, turn and size the selected
// layer, or its whole group.
//
//   the square in the middle   move freely (in 3D, along the ground)
//   the arrows                 move along one direction only:
//                              red left-right, green up-down, blue near-far
//   the ring                   turn (in 3D, about the upright)
//   the small square on it     size
//
// Shift while dragging: moves in steps of 0.1, turns in steps of 15 degrees,
// sizes in steps of 10%. Escape while dragging puts everything back.
import QtQuick

Item {
    id: root
    objectName: "gizmo"

    required property var viewport
    property bool threeD: false
    property var target: ({ visible: false })   // app.gizmo

    readonly property real armLength: 78
    readonly property real ringRadius: 62
    readonly property color xColor: "#ef5350"
    readonly property color yColor: "#66bb6a"
    readonly property color zColor: "#42a5f5"
    readonly property color ringColor: "#ffb74d"

    // Worked out from the camera whenever it, the effect or the selection changes.
    property bool shown: false
    property real px: 0
    property real py: 0
    property real unit: 1          // pixels per world unit at the pivot
    property var axes: []          // [{name, wx, wy, wz, sx, sy (pixels per unit), color}]
    property var ring: []          // the turning ring, as screen points
    property real sizeX: 0         // the size handle
    property real sizeY: 0

    // A drag.
    property string hot: ""        // the part under the mouse, or being dragged
    property string dragging: ""
    property real startX: 0
    property real startY: 0
    property real startPx: 0
    property real startPy: 0
    property var startAxes: []
    property real startUnit: 1
    property real turnSoFar: 0
    property real lastAngle: 0
    property string readout: ""

    signal begin(string kind)
    signal drag(real dx, real dy, real dz, real turn, real factor)
    signal end()
    signal cancel()

    function recompute() {
        var t = root.target
        if (!t || !t.visible || width <= 0 || !viewport) {
            shown = false
            canvas.requestPaint()
            return
        }
        var c = viewport.project(t.x, t.y, t.z)
        if (!c.ok) {
            shown = false
            canvas.requestPaint()
            return
        }
        px = c.x
        py = c.y
        unit = c.scale > 0 ? c.scale : 1
        var list = [
            { name: "x", wx: 1, wy: 0, wz: 0, color: xColor },
            { name: "y", wx: 0, wy: 1, wz: 0, color: yColor }
        ]
        if (threeD)
            list.push({ name: "z", wx: 0, wy: 0, wz: 1, color: zColor })
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var a = list[i]
            var p = viewport.project(t.x + a.wx, t.y + a.wy, t.z + a.wz)
            a.sx = p.ok ? p.x - px : 0
            a.sy = p.ok ? p.y - py : 0
            // Seen end on (pointing at the camera) an arrow is too short to grab.
            if (Math.sqrt(a.sx * a.sx + a.sy * a.sy) > 0.2 * unit)
                out.push(a)
        }
        axes = out
        // The ring: a circle in the picture for a 2D effect; for a 3D one,
        // a circle lying on the ground round the pivot, as the camera sees it.
        var pts = []
        if (threeD) {
            var r = ringRadius / unit
            for (var k = 0; k <= 48; ++k) {
                var ang = k / 48 * 2 * Math.PI
                var q = viewport.project(t.x + r * Math.cos(ang), t.y, t.z + r * Math.sin(ang))
                if (q.ok)
                    pts.push({ x: q.x, y: q.y })
            }
        } else {
            for (var j = 0; j <= 48; ++j) {
                var b = j / 48 * 2 * Math.PI
                pts.push({ x: px + ringRadius * Math.cos(b), y: py + ringRadius * Math.sin(b) })
            }
        }
        ring = pts
        // The size handle sits on the ring, up and to the right.
        var best = 0
        for (var m = 0; m < pts.length; ++m) {
            var score = (pts[m].x - px) - (pts[m].y - py)
            if (m === 0 || score > best) {
                best = score
                sizeX = pts[m].x
                sizeY = pts[m].y
            }
        }
        shown = true
        canvas.requestPaint()
    }

    onTargetChanged: recompute()
    onThreeDChanged: recompute()
    onWidthChanged: recompute()
    onHeightChanged: recompute()
    Connections {
        target: root.viewport
        function onViewChanged() { root.recompute() }
    }

    function distanceToSegment(x, y, ax, ay, bx, by) {
        var dx = bx - ax, dy = by - ay
        var len = dx * dx + dy * dy
        var t = len > 0 ? Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / len)) : 0
        var cx = ax + t * dx - x, cy = ay + t * dy - y
        return Math.sqrt(cx * cx + cy * cy)
    }

    function armEnd(a) {
        var l = Math.sqrt(a.sx * a.sx + a.sy * a.sy)
        return { x: px + a.sx / l * armLength, y: py + a.sy / l * armLength }
    }

    // Which part of the handles is at this point, or "" for none.
    function partAt(x, y) {
        if (!shown)
            return ""
        if (Math.abs(x - px) <= 9 && Math.abs(y - py) <= 9)
            return "center"
        if (Math.abs(x - sizeX) <= 8 && Math.abs(y - sizeY) <= 8)
            return "size"
        for (var i = 0; i < axes.length; ++i) {
            var e = armEnd(axes[i])
            if (distanceToSegment(x, y, px, py, e.x, e.y) <= 7)
                return axes[i].name
        }
        for (var k = 1; k < ring.length; ++k) {
            if (distanceToSegment(x, y, ring[k - 1].x, ring[k - 1].y, ring[k].x, ring[k].y) <= 6)
                return "ring"
        }
        return ""
    }

    function screenAngle(x, y) { return Math.atan2(-(y - startPy), x - startPx) * 180 / Math.PI }

    function snap(v, step, on) { return on ? Math.round(v / step) * step : v }

    // A screen movement as a movement in the world, along one arrow or
    // (the middle square) across a plane.
    function worldMove(dx, dy, part, shift) {
        var mv = [0, 0, 0]
        var find = function(n) {
            for (var i = 0; i < startAxes.length; ++i)
                if (startAxes[i].name === n)
                    return startAxes[i]
            return null
        }
        if (part === "x" || part === "y" || part === "z") {
            var a = find(part)
            if (!a)
                return mv
            var amount = (dx * a.sx + dy * a.sy) / (a.sx * a.sx + a.sy * a.sy)
            amount = snap(amount, 0.1, shift)
            mv = [a.wx * amount, a.wy * amount, a.wz * amount]
            return mv
        }
        // The middle: across the picture in 2D; along the ground in 3D,
        // unless the ground is seen nearly edge on.
        var ax = find("x"), ay = find("y"), az = find("z")
        var u = ax, w = threeD ? az : ay
        if (u && w) {
            var det = u.sx * w.sy - u.sy * w.sx
            var norm = Math.sqrt(u.sx * u.sx + u.sy * u.sy) * Math.sqrt(w.sx * w.sx + w.sy * w.sy)
            if (threeD && Math.abs(det) < 0.25 * norm && ay) {
                w = ay
                det = u.sx * w.sy - u.sy * w.sx
            }
            if (Math.abs(det) > 1e-6) {
                var s = (dx * w.sy - dy * w.sx) / det
                var r = (u.sx * dy - u.sy * dx) / det
                s = snap(s, 0.1, shift)
                r = snap(r, 0.1, shift)
                mv = [u.wx * s + w.wx * r, u.wy * s + w.wy * r, u.wz * s + w.wz * r]
            }
        }
        return mv
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            var g = getContext("2d")
            g.reset()
            if (!root.shown)
                return
            var hot = root.dragging !== "" ? root.dragging : root.hot
            g.lineCap = "round"
            // The ring.
            g.lineWidth = hot === "ring" ? 4 : 2
            g.strokeStyle = root.ringColor
            g.globalAlpha = hot === "ring" ? 1.0 : 0.75
            g.beginPath()
            for (var k = 0; k < root.ring.length; ++k) {
                if (k === 0)
                    g.moveTo(root.ring[k].x, root.ring[k].y)
                else
                    g.lineTo(root.ring[k].x, root.ring[k].y)
            }
            g.stroke()
            g.globalAlpha = 1.0
            // The arrows.
            for (var i = 0; i < root.axes.length; ++i) {
                var a = root.axes[i]
                var e = root.armEnd(a)
                var on = hot === a.name || hot === "center"
                g.strokeStyle = a.color
                g.fillStyle = a.color
                g.lineWidth = on ? 4 : 2.5
                g.beginPath()
                g.moveTo(root.px, root.py)
                g.lineTo(e.x, e.y)
                g.stroke()
                var ux = (e.x - root.px) / root.armLength, uy = (e.y - root.py) / root.armLength
                var head = on ? 14 : 11
                g.beginPath()
                g.moveTo(e.x + ux * head, e.y + uy * head)
                g.lineTo(e.x - uy * head * 0.45, e.y + ux * head * 0.45)
                g.lineTo(e.x + uy * head * 0.45, e.y - ux * head * 0.45)
                g.closePath()
                g.fill()
            }
            // The size handle.
            var big = hot === "size" ? 8 : 6
            g.fillStyle = "#ffffff"
            g.strokeStyle = root.ringColor
            g.lineWidth = 2
            g.fillRect(root.sizeX - big, root.sizeY - big, big * 2, big * 2)
            g.strokeRect(root.sizeX - big, root.sizeY - big, big * 2, big * 2)
            // The middle.
            var mid = hot === "center" ? 9 : 7
            g.fillStyle = hot === "center" ? "#ffffff" : "#ffe0b2"
            g.strokeStyle = "#202020"
            g.lineWidth = 1.5
            g.fillRect(root.px - mid, root.py - mid, mid * 2, mid * 2)
            g.strokeRect(root.px - mid, root.py - mid, mid * 2, mid * 2)
        }
    }

    Connections {
        target: root
        function onHotChanged() { canvas.requestPaint() }
        function onDraggingChanged() { canvas.requestPaint() }
    }

    // What is being done, while it is done.
    Rectangle {
        visible: root.dragging !== "" && root.readout !== ""
        x: Math.min(root.width - width - 6, root.px + 18)
        y: Math.max(6, root.py - root.ringRadius - 34)
        width: readoutText.implicitWidth + 16
        height: readoutText.implicitHeight + 8
        radius: 4
        color: "#d0202020"
        Text {
            id: readoutText
            anchors.centerIn: parent
            text: root.readout
            color: "white"
            font.pixelSize: 12
        }
    }

    // What the handles are for, by the pivot, when nothing is being dragged.
    Text {
        visible: root.shown && root.dragging === "" && root.hot !== ""
        x: Math.min(root.width - width - 6, root.px + 16)
        y: root.py + 14
        text: root.hot === "center" ? (root.threeD ? "Move along the ground" : "Move")
            : root.hot === "x" ? "Move left / right"
            : root.hot === "y" ? "Move up / down"
            : root.hot === "z" ? "Move nearer / further"
            : root.hot === "ring" ? (root.threeD ? "Turn about the upright" : "Turn")
            : root.hot === "size" ? "Size" : ""
        color: "white"
        style: Text.Outline
        styleColor: "#000000"
        font.pixelSize: 12
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.hot === "" ? Qt.ArrowCursor
                   : root.hot === "ring" ? Qt.CrossCursor
                   : root.hot === "size" ? Qt.SizeBDiagCursor : Qt.SizeAllCursor
        onPositionChanged: function(mouse) {
            if (root.dragging === "") {
                root.hot = root.partAt(mouse.x, mouse.y)
                return
            }
            var dx = mouse.x - root.startX, dy = mouse.y - root.startY
            var shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
            if (root.dragging === "ring") {
                var now = root.screenAngle(mouse.x, mouse.y)
                var step = now - root.lastAngle
                while (step > 180) step -= 360
                while (step < -180) step += 360
                root.turnSoFar += step
                root.lastAngle = now
                var turn = root.snap(root.turnSoFar, 15, shift)
                root.readout = "Turn " + turn.toFixed(0) + "°"
                root.drag(0, 0, 0, turn, 1)
            } else if (root.dragging === "size") {
                var d0 = Math.sqrt(Math.pow(root.startX - root.startPx, 2) + Math.pow(root.startY - root.startPy, 2))
                var d1 = Math.sqrt(Math.pow(mouse.x - root.startPx, 2) + Math.pow(mouse.y - root.startPy, 2))
                var f = d0 > 1 ? Math.max(0.05, Math.min(20, d1 / d0)) : 1
                f = shift ? Math.max(0.1, Math.round(f * 10) / 10) : f
                root.readout = "Size " + (f * 100).toFixed(0) + "%"
                root.drag(0, 0, 0, 0, f)
            } else {
                var mv = root.worldMove(dx, dy, root.dragging, shift)
                root.readout = "Move " + mv[0].toFixed(2) + ", " + mv[1].toFixed(2) + (root.threeD ? ", " + mv[2].toFixed(2) : "")
                root.drag(mv[0], mv[1], mv[2], 0, 1)
            }
        }
        onPressed: function(mouse) {
            var part = root.partAt(mouse.x, mouse.y)
            if (part === "") {
                mouse.accepted = false   // the camera's, not ours
                return
            }
            root.dragging = part
            root.startX = mouse.x
            root.startY = mouse.y
            root.startPx = root.px
            root.startPy = root.py
            root.startAxes = root.axes
            root.startUnit = root.unit
            root.turnSoFar = 0
            root.lastAngle = root.screenAngle(mouse.x, mouse.y)
            root.readout = ""
            root.forceActiveFocus()
            root.begin(part === "ring" ? "turn" : part === "size" ? "size" : "move")
        }
        onReleased: {
            if (root.dragging !== "") {
                root.dragging = ""
                root.end()
            }
        }
        onCanceled: {
            if (root.dragging !== "") {
                root.dragging = ""
                root.cancel()
            }
        }
        onExited: if (root.dragging === "") root.hot = ""
    }

    Keys.onEscapePressed: {
        if (root.dragging !== "") {
            root.dragging = ""
            root.cancel()
        }
    }
}
