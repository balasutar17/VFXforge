import QtQuick

// A one-line text box.
Rectangle {
    id: root

    property string text: ""
    signal edited(string newText)

    implicitWidth: 120
    implicitHeight: 26
    radius: 4
    color: theme.field
    border.width: 1
    border.color: input.activeFocus ? theme.accent : theme.line

    onTextChanged: if (!input.activeFocus) input.text = root.text
    Component.onCompleted: input.text = root.text

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        verticalAlignment: TextInput.AlignVCenter
        color: theme.text
        selectionColor: theme.accent
        selectedTextColor: theme.accentText
        font.pixelSize: theme.fontSize
        selectByMouse: true
        clip: true
        maximumLength: 200

        onEditingFinished: {
            if (text !== root.text)
                root.edited(text)
            text = root.text
        }
        Keys.onEscapePressed: {
            text = root.text
            focus = false
        }
        Keys.onReturnPressed: focus = false
        Keys.onEnterPressed: focus = false
    }
}
