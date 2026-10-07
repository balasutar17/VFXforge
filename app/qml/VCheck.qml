import QtQuick

// A tick box with a label.
Item {
    id: root

    property bool checked: false
    property string text: ""
    signal toggled(bool nowChecked)

    implicitHeight: 22
    implicitWidth: box.width + (label.text.length > 0 ? label.implicitWidth + 8 : 0)

    Rectangle {
        id: box
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 3
        color: root.checked ? theme.accent : theme.field
        border.width: 1
        border.color: root.checked ? theme.accent : theme.faint

        Rectangle {
            anchors.centerIn: parent
            visible: root.checked
            width: 8
            height: 8
            radius: 2
            color: theme.accentText
        }
    }

    Text {
        id: label
        anchors.left: box.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: theme.text
        font.pixelSize: theme.fontSize
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.toggled(!root.checked)
    }
}
