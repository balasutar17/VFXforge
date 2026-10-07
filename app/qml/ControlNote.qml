import QtQuick

// A control this version cannot edit yet. It says so instead of pretending.
Item {
    id: root

    property var info
    property int controlIndex: -1

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: 2

        Text {
            text: root.info.label
            color: theme.text
            font.pixelSize: theme.fontSize
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: theme.dim
            font.pixelSize: theme.smallFontSize
            text: root.info.kind === "curve"
                  ? "This value changes over time. Editing curves is NOT IMPLEMENTED yet, so it is left as it is."
                  : "This control is not connected to anything this version understands."
        }
    }
}
