import QtQuick

// A slider drawn by the app. It reports the start and end of a drag so the
// whole drag can be undone as one step.
Item {
    id: root

    property real value: 0
    property real from: 0
    property real to: 1
    property color fill: theme.accent
    // Optional picture behind the track (used by the colour sliders).
    property Gradient trackGradient: null

    signal dragStarted()
    signal dragEnded()
    signal moved(real newValue)

    implicitHeight: 20
    implicitWidth: 120

    readonly property real span: Math.max(1e-9, to - from)
    readonly property real position: Math.max(0, Math.min(1, (value - from) / span))

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        x: 7
        width: parent.width - 14
        height: root.trackGradient ? 10 : 4
        radius: height / 2
        color: theme.field
        border.color: theme.line
        border.width: root.trackGradient ? 0 : 1
        gradient: root.trackGradient

        Rectangle {
            visible: !root.trackGradient
            width: parent.width * root.position
            height: parent.height
            radius: parent.radius
            color: root.fill
        }
    }

    Rectangle {
        x: track.x + track.width * root.position - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        radius: 7
        color: area.pressed ? "#ffffff" : theme.text
        border.color: "#000000"
        border.width: 1
    }

    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true

        function valueAt(px) {
            var t = (px - track.x) / Math.max(1, track.width)
            t = Math.max(0, Math.min(1, t))
            return root.from + t * root.span
        }

        onPressed: function(mouse) {
            root.dragStarted()
            root.moved(valueAt(mouse.x))
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.moved(valueAt(mouse.x))
        }
        onReleased: root.dragEnded()
        onCanceled: root.dragEnded()
    }
}
