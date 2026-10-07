import QtQuick
import QtQuick.Controls.Basic

// A plain push button drawn by the app, so it looks the same everywhere.
Rectangle {
    id: root

    property string text: ""
    property string tip: ""
    property bool primary: false
    property bool checked: false
    property bool quiet: false   // no background until hovered

    signal clicked()

    implicitWidth: Math.max(28, label.implicitWidth + 20)
    implicitHeight: 28
    radius: 5
    opacity: enabled ? 1.0 : 0.38
    color: primary ? theme.accent
         : checked ? theme.hover
         : area.pressed ? theme.hover
         : area.containsMouse ? theme.hover
         : quiet ? "transparent" : theme.raised
    border.width: checked && !primary ? 1 : 0
    border.color: theme.accent

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.primary ? theme.accentText : theme.text
        font.pixelSize: theme.fontSize
        font.weight: root.primary ? Font.DemiBold : Font.Normal
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }

    ToolTip.visible: root.tip.length > 0 && area.containsMouse && !area.pressed
    ToolTip.delay: 600
    ToolTip.text: root.tip
}
