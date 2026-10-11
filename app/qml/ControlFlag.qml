import QtQuick

// A setting that is on or off.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property bool value: info.value === true

    implicitHeight: box.implicitHeight

    VCheck {
        id: box
        text: root.info.label
        checked: root.value
        onToggled: function(nowChecked) {
            root.value = nowChecked
            app.setControlFlag(root.controlIndex, nowChecked)
        }
    }
}
