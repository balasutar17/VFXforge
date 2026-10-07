import QtQuick

// The title line of one control: its name, its unit, and (for values that
// can vary from particle to particle) a Random tick box.
Item {
    id: root

    property string label: ""
    property string unit: ""
    property bool canBeRandom: false
    property bool random: false
    signal randomToggled(bool on)

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
        anchors.right: randomBox.visible ? randomBox.left : parent.right
        anchors.rightMargin: 8
        anchors.baseline: name.baseline
        text: root.unit
        color: theme.faint
        font.pixelSize: theme.smallFontSize
        elide: Text.ElideRight
    }
    VCheck {
        id: randomBox
        visible: root.canBeRandom
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: "Random"
        checked: root.random
        onToggled: function(nowChecked) { root.randomToggled(nowChecked) }
    }
}
