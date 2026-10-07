import QtQuick

// The heading of a panel section.
Item {
    property string text: ""
    property string detail: ""

    implicitHeight: 30

    Text {
        id: title
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: parent.text.toUpperCase()
        color: theme.dim
        font.pixelSize: theme.smallFontSize
        font.letterSpacing: 1.1
        font.weight: Font.DemiBold
    }
    Text {
        anchors.left: title.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: parent.detail
        color: theme.text
        font.pixelSize: theme.smallFontSize
        elide: Text.ElideRight
    }
}
