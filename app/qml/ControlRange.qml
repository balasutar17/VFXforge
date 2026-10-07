import QtQuick

// A random range: every particle picks its own value between two numbers.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property real low: info.from
    property real high: info.to
    readonly property int decimals: info.uiMax >= 100 ? 0 : (info.uiMax >= 10 ? 1 : 2)

    implicitHeight: column.implicitHeight

    function tidy(v) {
        v = Math.max(info.hardMin, Math.min(info.hardMax, v))
        return Number(v.toFixed(decimals + 1))
    }
    // Moving one end past the other pushes the other along with it.
    function applyLow(v) {
        root.low = tidy(v)
        if (root.high < root.low)
            root.high = root.low
        app.setControlRange(root.controlIndex, root.low, root.high)
    }
    function applyHigh(v) {
        root.high = tidy(v)
        if (root.low > root.high)
            root.low = root.high
        app.setControlRange(root.controlIndex, root.low, root.high)
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.info.unit
            canBeRandom: true
            random: true
            onRandomToggled: function(on) { app.setControlRandom(root.controlIndex, on) }
        }
        Item {
            width: parent.width
            height: 24
            Text {
                id: lowLabel
                width: 34
                anchors.verticalCenter: parent.verticalCenter
                text: "From"
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }
            VSlider {
                anchors.left: lowLabel.right
                anchors.right: lowBox.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: root.info.uiMin
                to: root.info.uiMax
                value: root.low
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.applyLow(v) }
            }
            VNumberField {
                id: lowBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                value: root.low
                decimals: root.decimals
                onEdited: function(v) { root.applyLow(v) }
            }
        }
        Item {
            width: parent.width
            height: 24
            Text {
                id: highLabel
                width: 34
                anchors.verticalCenter: parent.verticalCenter
                text: "To"
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }
            VSlider {
                anchors.left: highLabel.right
                anchors.right: highBox.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: root.info.uiMin
                to: root.info.uiMax
                value: root.high
                onDragStarted: app.beginEdit("Change " + root.info.label)
                onDragEnded: app.endEdit()
                onMoved: function(v) { root.applyHigh(v) }
            }
            VNumberField {
                id: highBox
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                value: root.high
                decimals: root.decimals
                onEdited: function(v) { root.applyHigh(v) }
            }
        }
    }
}
