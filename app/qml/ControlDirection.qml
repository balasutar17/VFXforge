import QtQuick

// A direction, turned like a dial. 3D effects get a second slider that
// tilts it toward or away from the viewer.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property real heading: info.heading
    property real tilt: info.tilt

    implicitHeight: column.implicitHeight

    function apply() {
        app.setControlDirection(root.controlIndex, root.heading, root.tilt)
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: "degrees"
        }
        Item {
            width: parent.width
            height: 28

            // A small compass showing which way particles are sent.
            Rectangle {
                id: dial
                width: 26
                height: 26
                radius: 13
                anchors.verticalCenter: parent.verticalCenter
                color: theme.field
                border.color: theme.line
                Rectangle {
                    x: 12
                    y: 12
                    width: 11
                    height: 2
                    radius: 1
                    color: theme.accent
                    transformOrigin: Item.Left
                    // Screen angles run clockwise; headings run counter-clockwise.
                    rotation: -root.heading
                }
            }
            VSlider {
                anchors.left: dial.right
                anchors.leftMargin: 6
                anchors.right: headingBox.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: 360
                value: root.heading
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.heading = Math.round(v); root.apply() }
            }
            VNumberField {
                id: headingBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                value: root.heading
                decimals: 0
                onEdited: function(v) {
                    root.heading = ((v % 360) + 360) % 360
                    root.apply()
                }
            }
        }
        Item {
            visible: root.info.threeD
            width: parent.width
            height: visible ? 24 : 0
            Text {
                id: tiltLabel
                width: 32
                anchors.verticalCenter: parent.verticalCenter
                text: "Tilt"
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }
            VSlider {
                anchors.left: tiltLabel.right
                anchors.right: tiltBox.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: -90
                to: 90
                value: root.tilt
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.tilt = Math.round(v); root.apply() }
            }
            VNumberField {
                id: tiltBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                value: root.tilt
                decimals: 0
                onEdited: function(v) {
                    root.tilt = Math.max(-90, Math.min(90, v))
                    root.apply()
                }
            }
        }
    }
}
