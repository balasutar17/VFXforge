import QtQuick

// The heading of one group of settings in the Advanced view. Click it to
// fold the group away or bring it back.
Rectangle {
    id: root

    property var info
    property int controlIndex: -1
    // Set by the panel, which remembers which groups are folded.
    property bool folded: false
    signal toggled()

    implicitHeight: 26
    radius: 4
    color: area.containsMouse ? theme.hover : theme.raised

    Text {
        id: arrow
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.folded ? "▸" : "▾"
        color: theme.dim
        font.pixelSize: theme.fontSize
    }
    Text {
        anchors.left: arrow.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: root.info.label.toUpperCase()
        color: theme.text
        font.pixelSize: theme.smallFontSize
        font.letterSpacing: 1.1
        font.weight: Font.DemiBold
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.toggled()
    }
}
