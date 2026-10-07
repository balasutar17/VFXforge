import QtQuick
import QtQuick.Controls.Basic
import VfxForge 1.0

// The particle's shape, picked from pictures of every built-in shape.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property string value: info.value

    implicitHeight: column.implicitHeight

    function labelOf(value) {
        for (var i = 0; i < info.options.length; ++i)
            if (info.options[i].value === value)
                return info.options[i].label
        return value
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.labelOf(root.value)
        }
        Flow {
            width: parent.width
            spacing: 4

            Repeater {
                model: root.info.options
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    readonly property bool chosen: root.value === modelData.value

                    width: 38
                    height: 38
                    radius: 5
                    color: chosen ? theme.hover : (tileArea.containsMouse ? theme.raised : theme.field)
                    border.width: 1
                    border.color: chosen ? theme.accent : theme.line

                    ShapeIcon {
                        anchors.fill: parent
                        anchors.margins: 5
                        shape: tile.modelData.value
                        color: tile.chosen ? theme.accent : "#c9ced8"
                    }
                    MouseArea {
                        id: tileArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.value = tile.modelData.value
                            app.setControlChoice(root.controlIndex, tile.modelData.value)
                        }
                    }
                    ToolTip.visible: tileArea.containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: tile.modelData.label
                }
            }
        }
    }
}
