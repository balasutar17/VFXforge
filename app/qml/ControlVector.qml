import QtQuick

// Three numbers that belong together: a push across, up and toward you, or
// a width, height and depth. A flat (2D) effect has no use for the third.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property real vx: info.x
    property real vy: info.y
    property real vz: info.z
    readonly property int decimals: info.uiMax >= 100 ? 0 : (info.uiMax >= 10 ? 1 : 2)
    readonly property bool deep: info.threeD === true

    implicitHeight: column.implicitHeight

    function tidy(v) {
        return Math.max(info.hardMin, Math.min(info.hardMax, v))
    }
    function apply() {
        app.setControlVector(root.controlIndex, root.vx, root.vy, root.vz)
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.info.unit
        }
        Row {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.deep ? 3 : 2
                delegate: Row {
                    id: cell
                    required property int index
                    spacing: 4

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.info.names[cell.index]
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                    }
                    VNumberField {
                        width: root.deep ? 50 : 64
                        decimals: root.decimals
                        value: cell.index === 0 ? root.vx : (cell.index === 1 ? root.vy : root.vz)
                        onEdited: function(v) {
                            var next = root.tidy(v)
                            if (cell.index === 0)
                                root.vx = next
                            else if (cell.index === 1)
                                root.vy = next
                            else
                                root.vz = next
                            root.apply()
                        }
                    }
                }
            }
        }
    }
}
