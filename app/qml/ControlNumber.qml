import QtQuick

// One number: a slider for feel, a box for an exact value.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property real value: info.value
    readonly property int decimals: info.uiMax >= 100 ? 0 : (info.uiMax >= 10 ? 1 : 2)

    implicitHeight: column.implicitHeight

    function tidy(v) {
        v = Math.max(info.hardMin, Math.min(info.hardMax, v))
        return Number(v.toFixed(decimals + 1))
    }
    function apply(v) {
        root.value = tidy(v)
        app.setControlNumber(root.controlIndex, root.value)
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.info.unit
            canBeRandom: root.info.canBeRandom
            random: false
            onRandomToggled: function(on) { app.setControlRandom(root.controlIndex, on) }
        }
        Item {
            width: parent.width
            height: 24
            VSlider {
                anchors.left: parent.left
                anchors.right: box.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: root.info.uiMin
                to: root.info.uiMax
                value: root.value
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.apply(v) }
            }
            VNumberField {
                id: box
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                value: root.value
                decimals: root.decimals
                onEdited: function(v) { root.apply(v) }
            }
        }
    }
}
