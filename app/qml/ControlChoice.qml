import QtQuick

// One choice out of a few, shown as a row of buttons.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property string value: info.value

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
        }
        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.info.options
                delegate: VButton {
                    required property var modelData
                    text: modelData.label
                    checked: root.value === modelData.value
                    onClicked: {
                        root.value = modelData.value
                        app.setControlChoice(root.controlIndex, modelData.value)
                    }
                }
            }
        }
    }
}
