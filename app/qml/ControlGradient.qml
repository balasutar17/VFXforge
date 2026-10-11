import QtQuick

// A colour that changes over each particle's life: a strip of colour with
// stops along it. Click a stop to choose it, drag it to move it, and
// double-click the strip to add one. Each stop has a colour, an opacity, and
// a brightness: above 1 it is brighter than the screen's white, which is
// what makes the start of a flame white-hot.
Item {
    id: root

    property var info
    property int controlIndex: -1

    // [{t, hue, sat, val, bright, alpha}] in order. Hue, saturation and
    // value are kept as numbers because a grey has no hue of its own, and
    // the hue slider would jump about if it were read back from the colour.
    property var stops: []
    property var strip: []   // the gradient as colours side by side
    property int selected: 0
    readonly property bool hasSelection: selected >= 0 && selected < stops.length
    readonly property int pieces: 48

    implicitHeight: column.implicitHeight

    function fromWire(list) {
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var c = list[i].color
            out.push({
                "t": Number(list[i].t),
                "hue": c.hsvHue < 0 ? 0 : c.hsvHue,
                "sat": c.hsvSaturation,
                "val": c.hsvValue,
                "bright": Number(list[i].bright),
                "alpha": Number(list[i].alpha)
            })
        }
        return out
    }
    function toWire(list) {
        var out = []
        for (var i = 0; i < list.length; ++i) {
            out.push({
                "t": list[i].t,
                "color": Qt.hsva(list[i].hue, list[i].sat, list[i].val, 1),
                "bright": list[i].bright,
                "alpha": list[i].alpha
            })
        }
        return out
    }
    function copied(list) {
        var out = []
        for (var i = 0; i < list.length; ++i)
            out.push({ "t": list[i].t, "hue": list[i].hue, "sat": list[i].sat, "val": list[i].val,
                       "bright": list[i].bright, "alpha": list[i].alpha })
        return out
    }

    Component.onCompleted: {
        stops = fromWire(info.stops)
        selected = 0
        strip = app.gradientStrip(toWire(stops), pieces)
    }

    // Hands the stops to the effect. The list here is kept as it is (the
    // stops are already in order and in range), so hues do not jump.
    function push(next) {
        stops = next
        var wire = toWire(next)
        app.setControlGradient(controlIndex, wire)
        strip = app.gradientStrip(wire, pieces)
    }
    function change(field, value) {
        if (!hasSelection)
            return
        var next = copied(stops)
        next[selected][field] = value
        push(next)
    }
    function moveStop(at, t) {
        if (at < 0 || at >= stops.length)
            return
        var next = copied(stops)
        var first = at > 0 ? next[at - 1].t + 0.004 : 0
        var last = at < next.length - 1 ? next[at + 1].t - 0.004 : 1
        next[at].t = Math.max(first, Math.min(last, t))
        push(next)
    }
    function addStop(t) {
        if (stops.length >= 32)
            return
        t = Math.max(0, Math.min(1, t))
        var at = 0
        while (at < stops.length && stops[at].t < t)
            ++at
        if ((at < stops.length && stops[at].t - t < 0.01) || (at > 0 && t - stops[at - 1].t < 0.01))
            return
        // The colour that is already there at that moment.
        var made = fromWire([app.gradientStopAt(toWire(stops), t)])[0]
        var next = copied(stops)
        next.splice(at, 0, made)
        app.beginEdit("Add a colour stop")
        push(next)
        app.endEdit()
        selected = at
    }
    function removeSelected() {
        if (!hasSelection || stops.length <= 1)
            return
        var next = copied(stops)
        next.splice(selected, 1)
        selected = Math.max(0, selected - 1)
        push(next)
    }

    Column {
        id: column
        width: parent.width
        spacing: 5

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: "over each particle's life"
        }

        // The strip: colour on top, opacity underneath (white is solid).
        Item {
            id: bar
            width: parent.width
            height: 46
            readonly property real pad: 7
            function tAt(x) { return (x - pad) / Math.max(1, width - 2 * pad) }
            function xAt(t) { return pad + t * (width - 2 * pad) }

            Rectangle {
                x: bar.pad - 1
                width: bar.width - 2 * bar.pad + 2
                height: 30
                color: theme.line
            }
            Repeater {
                model: root.strip.length
                delegate: Item {
                    id: piece
                    required property int index
                    readonly property color shade: piece.index < root.strip.length ? root.strip[piece.index] : "#000000"
                    x: bar.pad + piece.index * (bar.width - 2 * bar.pad) / root.strip.length
                    width: (bar.width - 2 * bar.pad) / root.strip.length + 0.6
                    y: 1
                    height: 28
                    Rectangle {
                        width: parent.width
                        height: 20
                        color: Qt.rgba(piece.shade.r, piece.shade.g, piece.shade.b, 1)
                    }
                    Rectangle {
                        y: 21
                        width: parent.width
                        height: 7
                        color: Qt.rgba(piece.shade.a, piece.shade.a, piece.shade.a, 1)
                    }
                }
            }
            // The stops, as markers hanging under the strip.
            Repeater {
                model: root.stops
                delegate: Rectangle {
                    id: marker
                    required property var modelData
                    required property int index
                    readonly property bool chosen: marker.index === root.selected
                    x: bar.xAt(marker.modelData.t) - width / 2
                    y: 31
                    width: chosen ? 13 : 11
                    height: chosen ? 14 : 12
                    radius: 3
                    color: Qt.hsva(marker.modelData.hue, marker.modelData.sat, marker.modelData.val, 1)
                    border.width: chosen ? 2 : 1
                    border.color: chosen ? "#ffffff" : theme.faint
                }
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                property int dragging: -1

                function stopAt(x) {
                    var best = -1, bestDistance = 10
                    for (var i = 0; i < root.stops.length; ++i) {
                        var d = Math.abs(bar.xAt(root.stops[i].t) - x)
                        if (d < bestDistance) {
                            bestDistance = d
                            best = i
                        }
                    }
                    return best
                }
                function finish() {
                    if (dragging >= 0) {
                        dragging = -1
                        app.endEdit()
                    }
                }
                onPressed: function(mouse) {
                    var hit = stopAt(mouse.x)
                    if (hit >= 0) {
                        root.selected = hit
                        dragging = hit
                        app.beginEdit("Change " + root.info.label)
                    }
                }
                onPositionChanged: function(mouse) {
                    if (dragging >= 0)
                        root.moveStop(dragging, bar.tAt(mouse.x))
                }
                onReleased: finish()
                onCanceled: finish()
                onDoubleClicked: function(mouse) {
                    finish()
                    if (stopAt(mouse.x) < 0)
                        root.addStop(bar.tAt(mouse.x))
                }
            }
        }

        Item {
            width: parent.width
            height: 24
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.hasSelection ? "Stop " + (root.selected + 1) + " of " + root.stops.length + ", at"
                                            : "Double-click the strip to add a stop."
                    color: theme.dim
                    font.pixelSize: theme.smallFontSize
                }
                VNumberField {
                    visible: root.hasSelection
                    width: 46
                    decimals: 2
                    value: root.hasSelection ? root.stops[root.selected].t : 0
                    onEdited: function(v) { root.moveStop(root.selected, v) }
                }
            }
            VButton {
                anchors.right: removeStop.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                height: 22
                text: "Add"
                tip: "Add a stop half way to the next one"
                enabled: root.stops.length < 32
                onClicked: {
                    var from = root.hasSelection ? root.stops[root.selected].t : 0
                    var to = root.hasSelection && root.selected + 1 < root.stops.length
                             ? root.stops[root.selected + 1].t : 1
                    root.addStop(to - from > 0.03 ? 0.5 * (from + to) : 0.5 * from)
                }
            }
            VButton {
                id: removeStop
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 22
                quiet: true
                text: "×"
                tip: "Remove this stop"
                enabled: root.hasSelection && root.stops.length > 1
                onClicked: root.removeSelected()
            }
        }

        // The chosen stop's colour: hue, strength of colour, lightness.
        VSlider {
            visible: root.hasSelection
            width: parent.width
            from: 0
            to: 1
            value: root.hasSelection ? root.stops[root.selected].hue : 0
            trackGradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#ff0000" }
                GradientStop { position: 0.1667; color: "#ffff00" }
                GradientStop { position: 0.3333; color: "#00ff00" }
                GradientStop { position: 0.5; color: "#00ffff" }
                GradientStop { position: 0.6667; color: "#0000ff" }
                GradientStop { position: 0.8333; color: "#ff00ff" }
                GradientStop { position: 1.0; color: "#ff0000" }
            }
            onDragStarted: app.beginEdit("Change " + root.info.label)
            onDragEnded: app.endEdit()
            onMoved: function(v) { root.change("hue", Math.min(v, 0.9999)) }
        }
        VSlider {
            visible: root.hasSelection
            width: parent.width
            from: 0
            to: 1
            value: root.hasSelection ? root.stops[root.selected].sat : 0
            trackGradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: root.hasSelection ? Qt.hsva(root.stops[root.selected].hue, 0, root.stops[root.selected].val, 1) : "#808080" }
                GradientStop { position: 1.0; color: root.hasSelection ? Qt.hsva(root.stops[root.selected].hue, 1, root.stops[root.selected].val, 1) : "#808080" }
            }
            onDragStarted: app.beginEdit("Change " + root.info.label)
            onDragEnded: app.endEdit()
            onMoved: function(v) { root.change("sat", v) }
        }
        VSlider {
            visible: root.hasSelection
            width: parent.width
            from: 0
            to: 1
            value: root.hasSelection ? root.stops[root.selected].val : 0
            trackGradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#000000" }
                GradientStop { position: 1.0; color: root.hasSelection ? Qt.hsva(root.stops[root.selected].hue, root.stops[root.selected].sat, 1, 1) : "#ffffff" }
            }
            onDragStarted: app.beginEdit("Change " + root.info.label)
            onDragEnded: app.endEdit()
            onMoved: function(v) { root.change("val", v) }
        }

        // Brighter than white, and how solid.
        Item {
            visible: root.hasSelection
            width: parent.width
            height: 24
            Text {
                id: brightLabel
                width: 62
                anchors.verticalCenter: parent.verticalCenter
                text: "Brightness"
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }
            VSlider {
                anchors.left: brightLabel.right
                anchors.right: brightBox.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: 1
                to: 4
                value: root.hasSelection ? root.stops[root.selected].bright : 1
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.change("bright", Number(v.toFixed(2))) }
            }
            VNumberField {
                id: brightBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 50
                decimals: 2
                value: root.hasSelection ? root.stops[root.selected].bright : 1
                onEdited: function(v) { root.change("bright", Math.max(1, Math.min(1000, v))) }
            }
        }
        Item {
            visible: root.hasSelection
            width: parent.width
            height: 24
            Text {
                id: alphaLabel
                width: 62
                anchors.verticalCenter: parent.verticalCenter
                text: "Opacity"
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }
            VSlider {
                anchors.left: alphaLabel.right
                anchors.right: alphaBox.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: 1
                value: root.hasSelection ? root.stops[root.selected].alpha : 1
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.change("alpha", Number(v.toFixed(3))) }
            }
            VNumberField {
                id: alphaBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 50
                decimals: 2
                value: root.hasSelection ? root.stops[root.selected].alpha : 1
                onEdited: function(v) { root.change("alpha", Math.max(0, Math.min(1, v))) }
            }
        }
    }
}
