import QtQuick
import QtQuick.Controls.Basic

// How the layer's particles are laid over what is behind them: Photoshop's
// list of blend modes, in its groups, as a drop-down menu.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property string value: info.value

    // The option that is chosen now.
    readonly property var current: {
        for (var i = 0; i < info.options.length; ++i)
            if (info.options[i].value === value)
                return info.options[i]
        return null
    }

    // What the artist should know about the chosen mode: the live preview
    // draws only the modes the graphics card can do by itself, and Unity
    // draws only Normal and Linear Dodge (Add).
    readonly property string note: {
        if (!current)
            return ""
        var text = ""
        if (current.live === false)
            text = "The live preview shows " + current.label + " as Normal for now. Exported frames and pictures draw it properly."
        if (current.unity && current.unity.length > 0)
            text += (text.length > 0 ? " " : "") + "Unity draws it as " + current.unity + "."
        return text
    }

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
        }

        VButton {
            id: opener
            objectName: "blendMenuButton"
            width: parent.width
            checked: menu.opened
            text: (root.current ? root.current.label : root.value) + "   ▾"
            tip: "How this layer is laid over what is behind it, as in Photoshop"
            onClicked: menu.opened ? menu.close() : menu.open()

            Popup {
                id: menu
                objectName: "blendMenu"
                y: opener.height + 2
                width: opener.width
                padding: 4
                margins: 8
                height: Math.min(list.implicitHeight + 8, 540)
                background: Rectangle { color: theme.panel; border.color: theme.line; radius: 8 }

                contentItem: Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: list.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Column {
                        id: list
                        width: parent.width

                        Repeater {
                            model: root.info.options
                            delegate: Item {
                                id: row
                                required property var modelData
                                required property int index
                                // A line between Photoshop's groups.
                                readonly property bool divides: index > 0 && root.info.options[index - 1].group !== modelData.group
                                readonly property bool chosen: root.value === modelData.value
                                width: list.width
                                height: 26 + (divides ? 9 : 0)

                                Rectangle {
                                    visible: row.divides
                                    anchors.top: parent.top
                                    anchors.topMargin: 4
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    height: 1
                                    color: theme.line
                                }
                                Rectangle {
                                    id: item
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 26
                                    radius: 5
                                    color: area.containsMouse ? theme.hover : (row.chosen ? theme.raised : "transparent")

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: row.chosen ? "✓" : ""
                                        color: theme.accent
                                        font.pixelSize: theme.fontSize
                                    }
                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 26
                                        anchors.right: parent.right
                                        anchors.rightMargin: 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: row.modelData.label
                                        color: theme.text
                                        font.pixelSize: theme.fontSize
                                        elide: Text.ElideRight
                                    }
                                    // Not drawn by the live preview yet.
                                    Text {
                                        visible: row.modelData.live === false
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "≈"
                                        color: theme.faint
                                        font.pixelSize: theme.fontSize
                                    }
                                    MouseArea {
                                        id: area
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            root.value = row.modelData.value
                                            app.setControlChoice(root.controlIndex, row.modelData.value)
                                            menu.close()
                                        }
                                    }
                                    ToolTip.visible: area.containsMouse && row.modelData.live === false
                                    ToolTip.delay: 500
                                    ToolTip.text: "The live preview shows this as Normal for now. Exported frames and pictures draw it properly."
                                }
                            }
                        }
                    }
                }
            }
        }

        Text {
            visible: root.note.length > 0
            width: parent.width
            text: root.note
            color: theme.faint
            font.pixelSize: theme.smallFontSize
            wrapMode: Text.WordWrap
        }
    }
}
