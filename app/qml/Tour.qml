import QtQuick

// The first-run tour: a few cards, each pointing at one part of the window
// and saying what it is for. Everything else is dimmed and cannot be touched
// until the tour is over, so a stray tap cannot change the effect.
//
// Each step is { title, body, desk, touch, targets }. "desk" is added on a
// Mac or PC and "touch" on an iPad; targets are the items to light up (none
// for a card in the middle of the window).
Item {
    id: tour
    objectName: "tour"

    property var steps: []
    property int index: 0
    property string finishText: "Done"
    readonly property bool touch: Qt.platform.os === "ios" || Qt.platform.os === "android"
    readonly property var step: steps.length > 0 ? steps[Math.min(index, steps.length - 1)] : null
    readonly property bool last: index >= steps.length - 1

    // Ended by its last button (true) or by skipping (false).
    signal finished(bool completed)

    function start() { showStep(0) }
    function showStep(n) {
        index = Math.max(0, Math.min(n, steps.length - 1))
        visible = true
        forceActiveFocus()
    }
    function next() { if (last) stop(true); else index += 1 }
    function back() { if (index > 0) index -= 1 }
    function stop(completed) {
        visible = false
        finished(completed === true)
    }

    // The lit-up part of the window: all of the step's targets together,
    // with a little room around them. Empty for a card in the middle.
    function holeFor(n, w, h) {
        var s = steps[n]
        if (!s || !s.targets || s.targets.length === 0)
            return Qt.rect(w / 2, h / 2, 0, 0)
        var x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
        for (var i = 0; i < s.targets.length; ++i) {
            var item = s.targets[i]
            if (!item || !item.visible)
                continue
            var r = tour.mapFromItem(item, 0, 0, item.width, item.height)
            x0 = Math.min(x0, r.x); y0 = Math.min(y0, r.y)
            x1 = Math.max(x1, r.x + r.width); y1 = Math.max(y1, r.y + r.height)
        }
        if (x1 < x0)
            return Qt.rect(w / 2, h / 2, 0, 0)
        var pad = 6
        x0 = Math.max(2, x0 - pad); y0 = Math.max(2, y0 - pad)
        x1 = Math.min(w - 2, x1 + pad); y1 = Math.min(h - 2, y1 + pad)
        return Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }
    readonly property rect hole: visible ? holeFor(index, width, height) : Qt.rect(0, 0, 0, 0)
    readonly property bool pointing: hole.width > 0

    anchors.fill: parent
    visible: false
    z: 1000

    Keys.onEscapePressed: stop(false)
    Keys.onRightPressed: next()
    Keys.onReturnPressed: next()
    Keys.onEnterPressed: next()
    Keys.onLeftPressed: back()

    // Taps and clicks outside the card go nowhere.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    // ---- the dimming, with a hole over the lit-up part
    Item {
        id: lit
        x: tour.hole.x; y: tour.hole.y
        width: tour.hole.width; height: tour.hole.height
        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }
    readonly property color shade: Qt.rgba(0, 0, 0, 0.62)
    Rectangle { color: tour.shade; x: 0; y: 0; width: tour.width; height: lit.y }
    Rectangle { color: tour.shade; x: 0; y: lit.y + lit.height; width: tour.width; height: tour.height - y }
    Rectangle { color: tour.shade; x: 0; y: lit.y; width: lit.x; height: lit.height }
    Rectangle { color: tour.shade; x: lit.x + lit.width; y: lit.y; width: tour.width - x; height: lit.height }

    // A ring round the lit-up part that breathes gently.
    Rectangle {
        x: lit.x - 2; y: lit.y - 2
        width: lit.width + 4; height: lit.height + 4
        visible: tour.pointing
        radius: 8
        color: "transparent"
        border.width: 2
        border.color: theme.accent
        SequentialAnimation on opacity {
            running: tour.visible && tour.pointing
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.45; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.45; to: 1.0; duration: 900; easing.type: Easing.InOutSine }
        }
    }

    // ---- the card
    Rectangle {
        id: card
        readonly property real gap: 14
        readonly property real room: 16
        width: Math.min(400, tour.width - 2 * room)
        height: content.implicitHeight + 36
        radius: 12
        color: theme.panel
        border.width: 1
        border.color: theme.line

        // Beside the lit-up part, on whichever side has room; in the middle
        // when nothing is lit.
        readonly property real below: tour.height - (lit.y + lit.height)
        readonly property real above: lit.y
        readonly property real rightOf: tour.width - (lit.x + lit.width)
        readonly property real leftOf: lit.x
        readonly property string side: !tour.pointing ? "middle"
            : below >= height + gap + room ? "below"
            : above >= height + gap + room ? "above"
            : rightOf >= width + gap + room ? "right"
            : leftOf >= width + gap + room ? "left" : "inside"
        function clampX(v) { return Math.max(room, Math.min(tour.width - width - room, v)) }
        function clampY(v) { return Math.max(room, Math.min(tour.height - height - room, v)) }
        x: side === "middle" ? (tour.width - width) / 2
         : side === "right" ? lit.x + lit.width + gap
         : side === "left" ? lit.x - width - gap
         : side === "inside" ? clampX(lit.x + (lit.width - width) / 2)
         : clampX(lit.x + lit.width / 2 - width / 2)
        y: side === "middle" ? (tour.height - height) / 2
         : side === "below" ? lit.y + lit.height + gap
         : side === "above" ? lit.y - height - gap
         : side === "inside" ? clampY(lit.y + lit.height - height - room)
         : clampY(lit.y + lit.height / 2 - height / 2)

        // Clicks on the card itself stay on the card.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        Column {
            id: content
            x: 18; y: 18
            width: card.width - 36
            spacing: 10

            Text {
                text: (tour.index + 1) + " of " + tour.steps.length
                color: theme.dim
                font.pixelSize: theme.smallFontSize
                font.letterSpacing: 1.0
            }
            Text {
                width: parent.width
                text: tour.step ? tour.step.title : ""
                color: theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: tour.step ? tour.step.body : ""
                color: theme.text
                font.pixelSize: 14
                lineHeight: 1.25
                wrapMode: Text.WordWrap
            }
            Text {
                readonly property string extra: !tour.step ? ""
                    : tour.touch ? (tour.step.touch || "") : (tour.step.desk || "")
                visible: extra !== ""
                width: parent.width
                text: extra
                color: theme.dim
                font.pixelSize: 13
                lineHeight: 1.2
                wrapMode: Text.WordWrap
            }

            // The dots, one per step.
            Row {
                spacing: 6
                topPadding: 4
                Repeater {
                    model: tour.steps.length
                    Rectangle {
                        required property int index
                        width: index === tour.index ? 16 : 6
                        height: 6
                        radius: 3
                        color: index === tour.index ? theme.accent : theme.line
                        Behavior on width { NumberAnimation { duration: 150 } }
                    }
                }
            }

            // Big enough to hit with a finger.
            Item {
                width: parent.width
                height: 40
                TourButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !tour.last
                    quiet: true
                    text: "Skip tour"
                    onClicked: tour.stop(false)
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    TourButton {
                        visible: tour.index > 0
                        text: "Back"
                        onClicked: tour.back()
                    }
                    TourButton {
                        primary: true
                        text: tour.last ? tour.finishText : tour.index === 0 ? "Show me" : "Next"
                        onClicked: tour.next()
                    }
                }
            }
        }

        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }

    component TourButton: Rectangle {
        id: button
        property string text: ""
        property bool primary: false
        property bool quiet: false
        signal clicked()
        implicitWidth: Math.max(72, label.implicitWidth + 32)
        implicitHeight: 40
        radius: 8
        color: primary ? theme.accent : pressArea.pressed || pressArea.containsMouse ? theme.hover
             : quiet ? "transparent" : theme.raised
        Text {
            id: label
            anchors.centerIn: parent
            text: button.text
            color: button.primary ? theme.accentText : button.quiet ? theme.dim : theme.text
            font.pixelSize: 14
            font.weight: button.primary ? Font.DemiBold : Font.Normal
        }
        MouseArea {
            id: pressArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }
}
