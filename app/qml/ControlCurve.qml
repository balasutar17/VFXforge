import QtQuick

// A value that changes over time, drawn as a curve you can reshape.
//
// Drag a point to move it. Double-click the graph to add one. A point is
// either a corner or smooth: the curve runs straight between corners and
// bends through smooth points. The ready-made shapes underneath set the
// whole curve at once. What is drawn is exactly what plays: the line comes
// from the same code the simulation uses.
Item {
    id: root

    property var info
    property int controlIndex: -1

    property var keys: []       // [{t, v, smooth}] in time order
    property var line: []       // the curve as short straight pieces: [{t, v}]
    property int selected: -1
    property real low: 0
    property real high: 1
    readonly property int decimals: (high - low) >= 100 ? 0 : ((high - low) >= 10 ? 1 : 2)
    readonly property bool hasSelection: selected >= 0 && selected < keys.length

    implicitHeight: column.implicitHeight

    function copied(list) {
        var out = []
        for (var i = 0; i < list.length; ++i)
            out.push({ "t": Number(list[i].t), "v": Number(list[i].v), "smooth": list[i].smooth === true })
        return out
    }

    Component.onCompleted: {
        keys = copied(info.keys)
        refit()
        redraw()
    }

    // The range of values the graph shows: the whole range for a setting
    // that only runs from 0 to 1, otherwise snug round the curve.
    function refit() {
        if (info.fixedRange === true) {
            low = 0
            high = 1
            return
        }
        var lo = 0, hi = 0
        for (var i = 0; i < keys.length; ++i) {
            lo = Math.min(lo, keys[i].v)
            hi = Math.max(hi, keys[i].v)
        }
        hi = hi > 0 ? hi * 1.35 : 0
        lo = lo < 0 ? lo * 1.35 : 0
        if (hi - lo < 1e-6)
            hi = info.uiMax > 0 ? info.uiMax : 1
        low = Math.max(info.hardMin, lo)
        high = Math.min(info.hardMax, hi)
        if (high - low < 1e-6)
            high = low + 1
    }
    function redraw() {
        line = app.curveLine(controlIndex, keys)
        graph.requestPaint()
    }
    // Hands the points to the effect and takes back the list as accepted.
    function push(next) {
        keys = copied(app.setControlCurve(controlIndex, next))
        redraw()
    }

    function moveKey(at, t, v) {
        if (at < 0 || at >= keys.length)
            return
        var next = copied(keys)
        var first = at > 0 ? next[at - 1].t + 0.004 : 0
        var last = at < next.length - 1 ? next[at + 1].t - 0.004 : 1
        next[at].t = Math.max(first, Math.min(last, t))
        next[at].v = Math.max(Math.max(low, info.hardMin), Math.min(Math.min(high, info.hardMax), v))
        push(next)
    }
    function valueAt(t) {
        if (line.length === 0)
            return 0
        if (t <= line[0].t)
            return line[0].v
        for (var i = 0; i + 1 < line.length; ++i) {
            if (t < line[i + 1].t) {
                var f = (t - line[i].t) / Math.max(1e-9, line[i + 1].t - line[i].t)
                return line[i].v + (line[i + 1].v - line[i].v) * f
            }
        }
        return line[line.length - 1].v
    }
    function addKey(t) {
        if (keys.length >= 32)
            return
        t = Math.max(0, Math.min(1, t))
        var next = copied(keys)
        var at = 0
        while (at < next.length && next[at].t < t)
            ++at
        // Too close to a point that is already there: select that one.
        if (at < next.length && next[at].t - t < 0.01) {
            selected = at
            graph.requestPaint()
            return
        }
        if (at > 0 && t - next[at - 1].t < 0.01) {
            selected = at - 1
            graph.requestPaint()
            return
        }
        // A new point between two smooth ones is smooth too.
        var smooth = (at > 0 && next[at - 1].smooth) || (at < next.length && next[at].smooth)
        next.splice(at, 0, { "t": t, "v": valueAt(t), "smooth": smooth })
        app.beginEdit("Add a point to " + info.label)
        push(next)
        app.endEdit()
        selected = at
        graph.requestPaint()
    }
    // In the middle of the widest gap, for anyone who has not found the
    // double-click.
    function addInWidestGap() {
        var bestAt = 0.5, bestGap = -1
        var edges = [0]
        for (var i = 0; i < keys.length; ++i)
            edges.push(keys[i].t)
        edges.push(1)
        for (var j = 0; j + 1 < edges.length; ++j) {
            if (edges[j + 1] - edges[j] > bestGap) {
                bestGap = edges[j + 1] - edges[j]
                bestAt = 0.5 * (edges[j] + edges[j + 1])
            }
        }
        addKey(bestAt)
    }
    function removeSelected() {
        if (!hasSelection || keys.length <= 2)
            return
        var next = copied(keys)
        next.splice(selected, 1)
        selected = -1
        push(next)
        refit()
        graph.requestPaint()
    }
    function setSelectedSmooth(on) {
        if (!hasSelection)
            return
        var next = copied(keys)
        next[selected].smooth = on
        push(next)
    }
    function useShape(key) {
        var next = app.applyCurveShape(controlIndex, key)
        if (next.length === 0)
            return
        keys = copied(next)
        selected = -1
        refit()
        redraw()
    }

    Column {
        id: column
        width: parent.width
        spacing: 6

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.info.unit
            canBeCurve: root.info.canBeCurve === true
            mode: "curve"
            onModeChosen: function(mode) { app.setControlMode(root.controlIndex, mode) }
        }

        Rectangle {
            width: parent.width
            height: 132
            radius: 5
            color: theme.field
            border.width: 1
            border.color: theme.line

            Canvas {
                id: graph
                anchors.fill: parent
                anchors.margins: 1

                readonly property real pad: 9
                function px(t) { return pad + t * (width - 2 * pad) }
                function py(v) { return height - pad - (v - root.low) / (root.high - root.low) * (height - 2 * pad) }
                function tAt(x) { return (x - pad) / Math.max(1, width - 2 * pad) }
                function vAt(y) { return root.low + (height - pad - y) / Math.max(1, height - 2 * pad) * (root.high - root.low) }

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.clearRect(0, 0, width, height)

                    // Quarter marks across, and the line where the value is nothing.
                    ctx.lineWidth = 1
                    ctx.strokeStyle = "" + theme.line
                    for (var q = 1; q < 4; ++q) {
                        var gx = Math.round(px(q / 4)) + 0.5
                        ctx.beginPath()
                        ctx.moveTo(gx, pad)
                        ctx.lineTo(gx, height - pad)
                        ctx.stroke()
                    }
                    if (root.low < 0 && root.high > 0) {
                        var zy = Math.round(py(0)) + 0.5
                        ctx.strokeStyle = "" + theme.faint
                        ctx.beginPath()
                        ctx.moveTo(pad, zy)
                        ctx.lineTo(width - pad, zy)
                        ctx.stroke()
                    }

                    var pts = root.line
                    if (pts.length > 0) {
                        // Level out to both edges, as the value is held there.
                        var floor = py(Math.max(root.low, Math.min(root.high, 0)))
                        ctx.beginPath()
                        ctx.moveTo(px(0), floor)
                        ctx.lineTo(px(0), py(pts[0].v))
                        for (var i = 0; i < pts.length; ++i)
                            ctx.lineTo(px(pts[i].t), py(pts[i].v))
                        ctx.lineTo(px(1), py(pts[pts.length - 1].v))
                        ctx.lineTo(px(1), floor)
                        ctx.closePath()
                        ctx.globalAlpha = 0.16
                        ctx.fillStyle = "" + theme.accent
                        ctx.fill()
                        ctx.globalAlpha = 1.0

                        ctx.beginPath()
                        ctx.moveTo(px(0), py(pts[0].v))
                        for (var k = 0; k < pts.length; ++k)
                            ctx.lineTo(px(pts[k].t), py(pts[k].v))
                        ctx.lineTo(px(1), py(pts[pts.length - 1].v))
                        ctx.lineWidth = 2
                        ctx.strokeStyle = "" + theme.accent
                        ctx.stroke()
                    }

                    // The points: round when smooth, square when a corner.
                    var keys = root.keys
                    for (var n = 0; n < keys.length; ++n) {
                        var x = px(keys[n].t), y = py(keys[n].v)
                        var chosen = n === root.selected
                        var r = chosen ? 6 : 4.5
                        ctx.beginPath()
                        if (keys[n].smooth)
                            ctx.arc(x, y, r, 0, Math.PI * 2)
                        else
                            ctx.rect(x - r, y - r, 2 * r, 2 * r)
                        ctx.fillStyle = chosen ? "#ffffff" : "" + theme.text
                        ctx.fill()
                        ctx.lineWidth = chosen ? 2 : 1
                        ctx.strokeStyle = chosen ? "" + theme.accent : "#000000"
                        ctx.stroke()
                    }
                }
            }

            MouseArea {
                anchors.fill: graph
                preventStealing: true
                property int dragging: -1

                function keyAt(x, y) {
                    var best = -1, bestDistance = 11 * 11
                    for (var i = 0; i < root.keys.length; ++i) {
                        var dx = graph.px(root.keys[i].t) - x, dy = graph.py(root.keys[i].v) - y
                        if (dx * dx + dy * dy < bestDistance) {
                            bestDistance = dx * dx + dy * dy
                            best = i
                        }
                    }
                    return best
                }
                function finish() {
                    if (dragging >= 0) {
                        dragging = -1
                        app.endEdit()
                        root.refit()
                        graph.requestPaint()
                    }
                }

                onPressed: function(mouse) {
                    var hit = keyAt(mouse.x, mouse.y)
                    root.selected = hit
                    dragging = hit
                    if (hit >= 0)
                        app.beginEdit("Change " + root.info.label)
                    graph.requestPaint()
                }
                onPositionChanged: function(mouse) {
                    if (dragging >= 0)
                        root.moveKey(dragging, graph.tAt(mouse.x), graph.vAt(mouse.y))
                }
                onReleased: finish()
                onCanceled: finish()
                onDoubleClicked: function(mouse) {
                    finish()
                    if (keyAt(mouse.x, mouse.y) < 0)
                        root.addKey(graph.tAt(mouse.x))
                }
            }
        }

        // What the two edges of the graph mean, and the range it shows.
        Item {
            width: parent.width
            height: 14
            Text {
                anchors.left: parent.left
                text: root.info.axis === "life" ? "born" : "layer starts"
                color: theme.faint
                font.pixelSize: theme.smallFontSize
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "top of graph: " + Number(root.high).toFixed(root.decimals)
                color: theme.faint
                font.pixelSize: theme.smallFontSize
            }
            Text {
                anchors.right: parent.right
                text: root.info.axis === "life" ? "dies" : "layer ends"
                color: theme.faint
                font.pixelSize: theme.smallFontSize
            }
        }

        // The selected point, exactly.
        Item {
            width: parent.width
            height: 26

            Text {
                visible: !root.hasSelection
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - addButton.width - 8
                elide: Text.ElideRight
                text: "Drag points. Double-click adds one."
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }
            Row {
                visible: root.hasSelection
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "At"
                    color: theme.dim
                    font.pixelSize: theme.smallFontSize
                }
                VNumberField {
                    width: 46
                    decimals: 2
                    value: root.hasSelection ? root.keys[root.selected].t : 0
                    onEdited: function(v) {
                        if (root.hasSelection)
                            root.moveKey(root.selected, v, root.keys[root.selected].v)
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "value"
                    color: theme.dim
                    font.pixelSize: theme.smallFontSize
                }
                VNumberField {
                    width: 54
                    decimals: root.decimals
                    value: root.hasSelection ? root.keys[root.selected].v : 0
                    onEdited: function(v) {
                        if (!root.hasSelection)
                            return
                        // Typing a value above the top of the graph is allowed: make room first.
                        var wanted = Math.max(root.info.hardMin, Math.min(root.info.hardMax, v))
                        if (root.info.fixedRange !== true) {
                            root.high = Math.max(root.high, wanted)
                            root.low = Math.min(root.low, wanted)
                        }
                        root.moveKey(root.selected, root.keys[root.selected].t, wanted)
                        root.refit()
                        graph.requestPaint()
                    }
                }
                VCheck {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Smooth"
                    checked: root.hasSelection && root.keys[root.selected].smooth === true
                    onToggled: function(nowChecked) { root.setSelectedSmooth(nowChecked) }
                }
            }
            VButton {
                id: removeButton
                visible: root.hasSelection
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 22
                quiet: true
                text: "×"
                tip: "Remove this point"
                enabled: root.keys.length > 2
                onClicked: root.removeSelected()
            }
            VButton {
                id: addButton
                visible: !root.hasSelection
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 22
                text: "Add a point"
                enabled: root.keys.length < 32
                onClicked: root.addInWidestGap()
            }
        }

        // Ready-made shapes. Those made for this setting alone come first
        // and are used as they are; the rest are set to stand as high as
        // the curve does now.
        Flow {
            width: parent.width
            spacing: 4

            Repeater {
                model: root.info.shapes !== undefined ? root.info.shapes : app.curveShapes
                delegate: VButton {
                    required property var modelData
                    height: 22
                    text: modelData.label
                    tip: modelData.help
                    onClicked: root.useShape(modelData.key)
                }
            }
        }
    }
}
