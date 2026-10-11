import QtQuick

// Bursts: groups of particles let go all at once, each at its own moment.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property var items: []   // [{time, count}], in time order

    implicitHeight: column.implicitHeight

    function copied(list) {
        var out = []
        for (var i = 0; i < list.length; ++i)
            out.push({ "time": Number(list[i].time), "count": Number(list[i].count) })
        return out
    }
    Component.onCompleted: items = copied(info.items)

    function push(next) {
        root.items = copied(app.setControlBursts(root.controlIndex, next))
    }
    function change(at, time, count) {
        var next = copied(root.items)
        next[at] = { "time": time, "count": count }
        push(next)
    }
    function add() {
        var next = copied(root.items)
        var when = next.length > 0 ? Math.min(root.info.layerLength, next[next.length - 1].time + 0.25) : 0
        var many = next.length > 0 ? next[next.length - 1].count : 20
        next.push({ "time": when, "count": many })
        push(next)
    }
    function remove(at) {
        var next = copied(root.items)
        next.splice(at, 1)
        push(next)
    }

    Column {
        id: column
        width: parent.width
        spacing: 4

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.items.length === 0 ? "none" : ""
        }
        Repeater {
            model: root.items
            delegate: Item {
                id: line
                required property var modelData
                required property int index
                width: column.width
                height: 24

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "At"
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                    }
                    VNumberField {
                        width: 56
                        decimals: 2
                        value: line.modelData.time
                        onEdited: function(v) { root.change(line.index, v, line.modelData.count) }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "s, let go"
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                    }
                    VNumberField {
                        width: 56
                        decimals: 0
                        value: line.modelData.count
                        onEdited: function(v) { root.change(line.index, line.modelData.time, Math.round(v)) }
                    }
                }
                VButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 22
                    quiet: true
                    text: "×"
                    tip: "Remove this burst"
                    onClicked: root.remove(line.index)
                }
            }
        }
        VButton {
            height: 24
            text: "Add a burst"
            enabled: root.items.length < 64
            onClicked: root.add()
        }
    }
}
