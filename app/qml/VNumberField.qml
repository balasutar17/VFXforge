import QtQuick

// A small box for typing an exact number.
Rectangle {
    id: root

    property real value: 0
    property int decimals: 2
    property string suffix: ""

    signal edited(real newValue)

    implicitWidth: 64
    implicitHeight: 24
    radius: 4
    color: theme.field
    border.width: 1
    border.color: input.activeFocus ? theme.accent : theme.line

    function show() {
        input.text = Number(root.value).toFixed(root.decimals)
    }
    onValueChanged: if (!input.activeFocus) show()
    onDecimalsChanged: show()
    Component.onCompleted: show()

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: TextInput.AlignRight
        color: theme.text
        selectionColor: theme.accent
        selectedTextColor: theme.accentText
        font.pixelSize: theme.fontSize
        selectByMouse: true
        clip: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly

        onActiveFocusChanged: if (activeFocus) selectAll()
        onEditingFinished: {
            var parsed = parseFloat(text.replace(",", "."))
            if (isFinite(parsed) && parsed !== root.value)
                root.edited(parsed)
            root.show()
        }
        Keys.onEscapePressed: {
            root.show()
            focus = false
        }
        Keys.onReturnPressed: focus = false
        Keys.onEnterPressed: focus = false
    }
}
