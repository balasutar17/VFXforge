// The handles over the viewport. They change the selection and nothing else:
// one layer, a group, or the layers picked with Cmd-click.
//
// Move (W)  the square in the middle moves freely (in 3D, along the ground);
//           each arrow moves along one axis: red X, green Y, blue Z.
// Turn (E)  each ring turns about its axis (in 2D, the one ring turns in the
//           picture; in 3D the green ring turns about the upright).
// Size (R)  the square in the middle makes everything larger or smaller;
//           each axis's box stretches the emitters along that axis only.
//
// Option-drag the middle square to move the pivot alone. Shift while
// dragging snaps (0.1 units, 15 degrees, 10%). Escape puts everything back.
// A click away from the handles selects the layer whose emitter (a small
// dot) is under the mouse, or nothing.
import QtQuick

Item {
    id: root
    objectName: "gizmo"

    required property var viewport
    property bool threeD: false
    property var target: ({ visible: false })   // app.gizmo
    property string mode: "move"                 // app.gizmoMode

    readonly property real armLength: 80
    readonly property real ringRadius: 64
    readonly property var axisColors: ["#ef5350", "#66bb6a", "#42a5f5"]
    readonly property color centerColor: "#ffe0b2"

    // Worked out from the camera whenever it, the effect or the selection changes.
    property bool shown: false
    property bool locked: false
    property real px: 0
    property real py: 0
    property real unit: 1          // pixels per world unit at the pivot
    property var axes: []          // [{i, w: [x, y, z], sx, sy}] the gizmo's axes (world or the layer's own)
    property var worldAxes: []     // the same for the world's axes (Size always works along these)
    property var rings: []         // [{i, pts: [{x, y}], sign}]
    property var marks: []         // every layer's emitter on screen: [{x, y, index, selected}]

    // A drag.
    property string hot: ""
    property string dragging: ""
    property real startX: 0
    property real startY: 0
    property real startPx: 0
    property real startPy: 0
    property var startAxes: []
    property var startWorld: []
    property var startRings: []
    property real turnSoFar: 0
    property real lastAngle: 0
    property var lastPivotMove: [0, 0, 0]
    property string readout: ""

    signal begin(string kind)
    signal drag(var change)
    signal end()
    signal cancel()
    signal pivotMoved(real dx, real dy, real dz)
    signal picked(int index, int modifiers)

    function screenOf(w) {
        var t = root.target
        var p = viewport.project(t.x + w[0], t.y + w[1], t.z + w[2])
        return { sx: p.ok ? p.x - px : 0, sy: p.ok ? p.y - py : 0 }
    }

    function recompute() {
        var t = root.target
        // The emitters, always (for clicking on them).
        var ms = []
        var list = t && t.markers ? t.markers : []
        for (var m = 0; m < list.length && viewport; ++m) {
            var q = viewport.project(list[m].x, list[m].y, list[m].z)
            if (q.ok)
                ms.push({ x: q.x, y: q.y, index: list[m].index, selected: list[m].selected })
        }
        marks = ms
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
        locked = t.locked === true
        px = c.x
        py = c.y
        unit = c.scale > 0 ? c.scale : 1
        var count = threeD ? 3 : 2
        var given = t.axes || [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        var out = [], world = []
        for (var i = 0; i < count; ++i) {
            var s = screenOf(given[i])
            out.push({ i: i, w: given[i], sx: s.sx, sy: s.sy })
            var wv = [i === 0 ? 1 : 0, i === 1 ? 1 : 0, i === 2 ? 1 : 0]
            var ws = screenOf(wv)
            world.push({ i: i, w: wv, sx: ws.sx, sy: ws.sy })
        }
        axes = out
        worldAxes = world
        // The rings: about Z only in 2D; about each axis in 3D. A ring lies
        // across the two other axes, and a turn the right way round carries
        // the first of them towards the second.
        var rs = []
        var ringAxes = threeD ? [0, 1, 2] : [2]
        var r = ringRadius / unit
        for (var k = 0; k < ringAxes.length; ++k) {
            var a = ringAxes[k]
            var u = given[(a + 1) % 3], v = given[(a + 2) % 3]
            var pts = []
            for (var j = 0; j <= 64; ++j) {
                var ang = j / 64 * 2 * Math.PI
                var cs = Math.cos(ang), sn = Math.sin(ang)
                var p = viewport.project(t.x + r * (cs * u[0] + sn * v[0]),
                                         t.y + r * (cs * u[1] + sn * v[1]),
                                         t.z + r * (cs * u[2] + sn * v[2]))
                if (p.ok)
                    pts.push({ x: p.x, y: p.y })
            }
            var su = screenOf(u), sv = screenOf(v)
            // Counter-clockwise on screen (y up) from u to v: a turn the right way round.
            var cross = su.sx * (-sv.sy) - (-su.sy) * sv.sx
            rs.push({ i: a, pts: pts, sign: cross >= 0 ? 1 : -1 })
        }
        rings = rs
        shown = true
        canvas.requestPaint()
    }

    onTargetChanged: recompute()
    onThreeDChanged: recompute()
    onModeChanged: { hot = ""; recompute() }
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
        if (l < 1e-6)
            return { x: px, y: py }
        return { x: px + a.sx / l * armLength, y: py + a.sy / l * armLength }
    }
    function usable(a) { return Math.sqrt(a.sx * a.sx + a.sy * a.sy) > 0.15 * unit }

    // Which part of the handles is at this point, or "" for none.
    function partAt(x, y) {
        if (!shown || locked)
            return ""
        if (Math.abs(x - px) <= 9 && Math.abs(y - py) <= 9)
            return "center"
        if (mode === "move") {
            for (var i = 0; i < axes.length; ++i) {
                var e = armEnd(axes[i])
                if (usable(axes[i]) && distanceToSegment(x, y, px, py, e.x, e.y) <= 7)
                    return "a" + axes[i].i
            }
        } else if (mode === "size") {
            for (var j = 0; j < worldAxes.length; ++j) {
                var f = armEnd(worldAxes[j])
                if (usable(worldAxes[j]) && Math.abs(x - f.x) <= 8 && Math.abs(y - f.y) <= 8)
                    return "s" + worldAxes[j].i
            }
            for (var j2 = 0; j2 < worldAxes.length; ++j2) {
                var f2 = armEnd(worldAxes[j2])
                if (usable(worldAxes[j2]) && distanceToSegment(x, y, px, py, f2.x, f2.y) <= 6)
                    return "s" + worldAxes[j2].i
            }
        } else {
            var best = "", bestD = 7
            for (var k = 0; k < rings.length; ++k) {
                var pts = rings[k].pts
                for (var n = 1; n < pts.length; ++n) {
                    var d = distanceToSegment(x, y, pts[n - 1].x, pts[n - 1].y, pts[n].x, pts[n].y)
                    if (d < bestD) {
                        bestD = d
                        best = "r" + rings[k].i
                    }
                }
            }
            return best
        }
        return ""
    }

    // The layer whose emitter dot is under the mouse, or -1.
    function markAt(x, y) {
        var best = -1, bestD = 12
        for (var i = 0; i < marks.length; ++i) {
            var d = Math.sqrt(Math.pow(marks[i].x - x, 2) + Math.pow(marks[i].y - y, 2))
            if (d < bestD) {
                bestD = d
                best = marks[i].index
            }
        }
        return best
    }

    function screenAngle(x, y) { return Math.atan2(-(y - startPy), x - startPx) * 180 / Math.PI }
    function snap(v, step, on) { return on ? Math.round(v / step) * step : v }

    // A screen movement as a world movement: along one axis, or (the middle
    // square) across the picture in 2D and along the ground in 3D.
    function worldMove(dx, dy, part, shift) {
        var list = startAxes
        if (part.charAt(0) === "a") {
            var a = list[parseInt(part.charAt(1))]
            var len2 = a.sx * a.sx + a.sy * a.sy
            if (len2 < 1e-9)
                return [0, 0, 0]
            var amount = snap((dx * a.sx + dy * a.sy) / len2, 0.1, shift)
            return [a.w[0] * amount, a.w[1] * amount, a.w[2] * amount]
        }
        var u = list[0], w = threeD ? list[2] : list[1]
        var det = u.sx * w.sy - u.sy * w.sx
        var norm = Math.sqrt(u.sx * u.sx + u.sy * u.sy) * Math.sqrt(w.sx * w.sx + w.sy * w.sy)
        if (threeD && Math.abs(det) < 0.25 * norm) {
            w = list[1]   // the ground seen edge on: up and down instead
            det = u.sx * w.sy - u.sy * w.sx
        }
        if (Math.abs(det) < 1e-6)
            return [0, 0, 0]
        var s = snap((dx * w.sy - dy * w.sx) / det, 0.1, shift)
        var r = snap((u.sx * dy - u.sy * dx) / det, 0.1, shift)
        return [u.w[0] * s + w.w[0] * r, u.w[1] * s + w.w[1] * r, u.w[2] * s + w.w[2] * r]
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            var g = getContext("2d")
            g.reset()
            // Every layer's emitter: a small dot, filled when selected.
            for (var m = 0; m < root.marks.length; ++m) {
                g.beginPath()
                g.arc(root.marks[m].x, root.marks[m].y, 3, 0, 2 * Math.PI)
                g.lineWidth = 1
                g.strokeStyle = "#d0ffffff"
                g.fillStyle = root.marks[m].selected ? "#ffb74d" : "#40000000"
                g.fill()
                g.stroke()
            }
            if (!root.shown)
                return
            var hot = root.dragging !== "" ? root.dragging : root.hot
            g.lineCap = "round"
            if (root.locked) {
                g.fillStyle = "#909090"
                g.fillRect(root.px - 7, root.py - 7, 14, 14)
                return
            }
            if (root.mode === "move") {
                for (var i = 0; i < root.axes.length; ++i) {
                    var a = root.axes[i]
                    if (!root.usable(a))
                        continue
                    var e = root.armEnd(a)
                    var on = hot === "a" + a.i || hot === "center"
                    g.strokeStyle = root.axisColors[a.i]
                    g.fillStyle = root.axisColors[a.i]
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
            } else if (root.mode === "size") {
                for (var j = 0; j < root.worldAxes.length; ++j) {
                    var b = root.worldAxes[j]
                    if (!root.usable(b))
                        continue
                    var f = root.armEnd(b)
                    var lit = hot === "s" + b.i || hot === "center"
                    g.strokeStyle = root.axisColors[b.i]
                    g.fillStyle = root.axisColors[b.i]
                    g.lineWidth = lit ? 4 : 2.5
                    g.beginPath()
                    g.moveTo(root.px, root.py)
                    g.lineTo(f.x, f.y)
                    g.stroke()
                    var box = lit ? 8 : 6
                    g.fillRect(f.x - box, f.y - box, box * 2, box * 2)
                }
            } else {
                for (var k = 0; k < root.rings.length; ++k) {
                    var ring = root.rings[k]
                    var lit2 = hot === "r" + ring.i
                    g.strokeStyle = root.threeD ? root.axisColors[ring.i] : "#ffb74d"
                    g.lineWidth = lit2 ? 4 : 2
                    g.globalAlpha = lit2 || hot === "" ? 1.0 : 0.55
                    g.beginPath()
                    for (var n = 0; n < ring.pts.length; ++n) {
                        if (n === 0)
                            g.moveTo(ring.pts[n].x, ring.pts[n].y)
                        else
                            g.lineTo(ring.pts[n].x, ring.pts[n].y)
                    }
                    g.stroke()
                    g.globalAlpha = 1.0
                }
            }
            // The middle.
            var mid = hot === "center" ? 9 : 7
            g.fillStyle = hot === "center" ? "#ffffff" : root.centerColor
            g.strokeStyle = "#202020"
            g.lineWidth = 1.5
            if (root.mode === "size") {
                g.fillRect(root.px - mid, root.py - mid, mid * 2, mid * 2)
                g.strokeRect(root.px - mid, root.py - mid, mid * 2, mid * 2)
            } else {
                g.beginPath()
                g.arc(root.px, root.py, mid, 0, 2 * Math.PI)
                g.fill()
                g.stroke()
            }
        }
    }

    Connections {
        target: root
        function onHotChanged() { canvas.requestPaint() }
        function onDraggingChanged() { canvas.requestPaint() }
        function onMarksChanged() { canvas.requestPaint() }
    }

    // What is being done, while it is done; what the handles are for when not.
    Rectangle {
        visible: root.shown && (root.dragging !== "" ? root.readout !== "" : root.hot !== "" || root.locked)
        x: Math.max(6, Math.min(root.width - width - 6, root.px + 18))
        y: Math.max(6, root.py - root.ringRadius - 34)
        width: readoutText.implicitWidth + 16
        height: readoutText.implicitHeight + 8
        radius: 4
        color: "#d0202020"
        Text {
            id: readoutText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
            text: {
                var who = root.target && root.target.label ? root.target.label : ""
                if (root.locked)
                    return who + " is locked"
                if (root.dragging !== "")
                    return root.readout
                var axisName = function(c) { return ["X (left / right)", "Y (up / down)", "Z (near / far)"][parseInt(c)] }
                if (root.hot === "center")
                    return root.mode === "size" ? "Size " + who
                         : root.mode === "turn" ? "Option-drag to move the pivot"
                         : (root.threeD ? "Move " + who + " along the ground" : "Move " + who) + "  ·  Option-drag: pivot"
                if (root.hot.charAt(0) === "a")
                    return "Move along " + axisName(root.hot.charAt(1))
                if (root.hot.charAt(0) === "s")
                    return "Stretch along " + axisName(root.hot.charAt(1))
                if (root.hot.charAt(0) === "r")
                    return "Turn about " + axisName(root.hot.charAt(1))
                return ""
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.hot === "" ? Qt.ArrowCursor
                   : root.hot.charAt(0) === "r" ? Qt.CrossCursor
                   : root.hot.charAt(0) === "s" || (root.mode === "size" && root.hot === "center") ? Qt.SizeAllCursor
                   : Qt.SizeAllCursor
        onPositionChanged: function(mouse) {
            if (root.dragging === "") {
                root.hot = root.partAt(mouse.x, mouse.y)
                return
            }
            var dx = mouse.x - root.startX, dy = mouse.y - root.startY
            var shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
            var d = root.dragging
            if (d === "pivot") {
                var p = root.worldMove(dx, dy, "center", shift)
                root.pivotMoved(p[0] - root.lastPivotMove[0], p[1] - root.lastPivotMove[1], p[2] - root.lastPivotMove[2])
                root.lastPivotMove = p
                root.readout = "Pivot " + p[0].toFixed(2) + ", " + p[1].toFixed(2) + (root.threeD ? ", " + p[2].toFixed(2) : "")
            } else if (d.charAt(0) === "r") {
                var now = root.screenAngle(mouse.x, mouse.y)
                var step = now - root.lastAngle
                while (step > 180) step -= 360
                while (step < -180) step += 360
                root.turnSoFar += step
                root.lastAngle = now
                var axis = parseInt(d.charAt(1))
                var sign = 1
                for (var k = 0; k < root.startRings.length; ++k)
                    if (root.startRings[k].i === axis)
                        sign = root.startRings[k].sign
                var turn = root.snap(root.turnSoFar * sign, 15, shift)
                root.readout = "Turn " + turn.toFixed(0) + "°"
                root.drag({ turn: turn, axis: axis })
            } else if (d.charAt(0) === "s") {
                var i = parseInt(d.charAt(1))
                var a = root.startWorld[i]
                var l = Math.sqrt(a.sx * a.sx + a.sy * a.sy)
                var along = l > 1e-6 ? (dx * a.sx + dy * a.sy) / l : 0
                var f = Math.max(0.05, 1 + along / root.armLength)
                f = shift ? Math.max(0.1, Math.round(f * 10) / 10) : f
                var sc = [1, 1, 1]
                sc[i] = f
                root.readout = "Stretch " + ["X", "Y", "Z"][i] + " " + (f * 100).toFixed(0) + "%"
                root.drag({ scale: sc })
            } else if (d === "center" && root.mode === "size") {
                var g = Math.max(0.05, Math.min(20, Math.exp((dx - dy) / 150)))
                g = shift ? Math.max(0.1, Math.round(g * 10) / 10) : g
                root.readout = "Size " + (g * 100).toFixed(0) + "%"
                root.drag({ factor: g })
            } else {
                var mv = root.worldMove(dx, dy, d === "center" ? "center" : d, shift)
                root.readout = "Move " + mv[0].toFixed(2) + ", " + mv[1].toFixed(2) + (root.threeD ? ", " + mv[2].toFixed(2) : "")
                root.drag({ move: mv })
            }
        }
        onPressed: function(mouse) {
            var part = root.partAt(mouse.x, mouse.y)
            if (part === "" || (part === "center" && root.mode === "turn" && !(mouse.modifiers & Qt.AltModifier))) {
                // Not ours: a click on an emitter dot selects its layer; a
                // click on nothing deselects. Drags are the camera's.
                if (root.markAt(mouse.x, mouse.y) >= 0 && (mouse.modifiers & Qt.AltModifier) === 0) {
                    root.picked(root.markAt(mouse.x, mouse.y), mouse.modifiers)
                    return
                }
                mouse.accepted = false
                return
            }
            root.startX = mouse.x
            root.startY = mouse.y
            root.startPx = root.px
            root.startPy = root.py
            root.startAxes = root.axes
            root.startWorld = root.worldAxes
            root.startRings = root.rings
            root.turnSoFar = 0
            root.lastAngle = root.screenAngle(mouse.x, mouse.y)
            root.lastPivotMove = [0, 0, 0]
            root.readout = ""
            root.forceActiveFocus()
            if (part === "center" && (mouse.modifiers & Qt.AltModifier)) {
                root.dragging = "pivot"
                return
            }
            root.dragging = part
            root.begin(part.charAt(0) === "r" ? "turn"
                     : part.charAt(0) === "s" || (part === "center" && root.mode === "size") ? "size" : "move")
        }
        onReleased: {
            if (root.dragging === "pivot") {
                root.dragging = ""
            } else if (root.dragging !== "") {
                root.dragging = ""
                root.end()
            }
        }
        onCanceled: {
            if (root.dragging !== "" && root.dragging !== "pivot")
                root.cancel()
            root.dragging = ""
        }
        onExited: if (root.dragging === "") root.hot = ""
    }

    Keys.onEscapePressed: {
        if (root.dragging !== "" && root.dragging !== "pivot")
            root.cancel()
        root.dragging = ""
    }
}
