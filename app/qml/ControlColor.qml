import QtQuick

// A colour, picked with hue, saturation and brightness sliders.
Item {
    id: root

    property var info
    property int controlIndex: -1

    // Kept here rather than read back from the colour, because a grey has no
    // hue of its own and the hue slider would jump while dragging.
    property real hue: 0
    property real saturation: 0
    property real brightness: 1
    property real alpha: 1
    readonly property color shown: Qt.hsva(hue, saturation, brightness, 1)

    implicitHeight: column.implicitHeight

    Component.onCompleted: {
        var c = info.color
        hue = c.hsvHue < 0 ? 0 : c.hsvHue
        saturation = c.hsvSaturation
        brightness = c.hsvValue
        alpha = c.a
    }

    function apply() {
        app.setControlColor(root.controlIndex, Qt.hsva(hue, saturation, brightness, alpha))
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        Item {
            width: parent.width
            height: 22
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.info.label
                color: theme.text
                font.pixelSize: theme.fontSize
                font.weight: Font.DemiBold
            }
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                height: 18
                radius: 4
                color: root.shown
                border.color: theme.line
            }
        }

        VSlider {
            width: parent.width
            from: 0
            to: 1
            value: root.hue
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
            onMoved: function(v) { root.hue = Math.min(v, 0.9999); root.apply() }
        }
        VSlider {
            width: parent.width
            from: 0
            to: 1
            value: root.saturation
            trackGradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.hsva(root.hue, 0, root.brightness, 1) }
                GradientStop { position: 1.0; color: Qt.hsva(root.hue, 1, root.brightness, 1) }
            }
            onDragStarted: app.beginEdit("Change " + root.info.label)
            onDragEnded: app.endEdit()
            onMoved: function(v) { root.saturation = v; root.apply() }
        }
        VSlider {
            width: parent.width
            from: 0
            to: 1
            value: root.brightness
            trackGradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#000000" }
                GradientStop { position: 1.0; color: Qt.hsva(root.hue, root.saturation, 1, 1) }
            }
            onDragStarted: app.beginEdit("Change " + root.info.label)
            onDragEnded: app.endEdit()
            onMoved: function(v) { root.brightness = v; root.apply() }
        }
    }
}
