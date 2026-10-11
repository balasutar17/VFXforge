import QtQuick

// The title line of one control: its name, its unit, and (for values that
// can vary) how it varies. In the Simple view that is a Random tick box; in
// the Advanced view a choice of Fixed, Random or Curve.
Item {
    id: root

    property string label: ""
    property string unit: ""
    property bool canBeRandom: false
    property bool random: false
    signal randomToggled(bool on)

    // Advanced: "fixed", "random" or "curve".
    property bool canBeCurve: false
    property string mode: "fixed"
    signal modeChosen(string mode)

    implicitHeight: 22

    Text {
        id: name
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: theme.text
        font.pixelSize: theme.fontSize
        font.weight: Font.DemiBold
    }
    Text {
        anchors.left: name.right
        anchors.leftMargin: 8
        anchors.right: modes.visible ? modes.left : (randomBox.visible ? randomBox.left : parent.right)
        anchors.rightMargin: 8
        anchors.baseline: name.baseline
        text: root.unit
        color: theme.faint
        font.pixelSize: theme.smallFontSize
        elide: Text.ElideRight
    }
    VCheck {
        id: randomBox
        visible: root.canBeRandom && !root.canBeCurve
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: "Random"
        checked: root.random
        onToggled: function(nowChecked) { root.randomToggled(nowChecked) }
    }
    Row {
        id: modes
        visible: root.canBeCurve
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Repeater {
            model: [
                { "mode": "fixed", "label": "Fixed" },
                { "mode": "random", "label": "Random" },
                { "mode": "curve", "label": "Curve" }
            ]
            delegate: Rectangle {
                id: chip
                required property var modelData
                readonly property bool current: root.mode === modelData.mode
                width: word.implicitWidth + 12
                height: 20
                radius: 4
                color: current ? theme.hover : (chipArea.containsMouse ? theme.raised : "transparent")
                border.width: current ? 1 : 0
                border.color: theme.accent

                Text {
                    id: word
                    anchors.centerIn: parent
                    text: chip.modelData.label
                    color: chip.current ? theme.text : theme.dim
                    font.pixelSize: theme.smallFontSize
                }
                MouseArea {
                    id: chipArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: if (!chip.current) root.modeChosen(chip.modelData.mode)
                }
            }
        }
    }
}
