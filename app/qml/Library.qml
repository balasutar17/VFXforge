import QtQuick
import VfxForge 1.0

// The effect library: every preset, playing, in a gallery.
Rectangle {
    id: root

    // Asked for by a card. The window decides what happens to unsaved work.
    signal openRequested(string presetId)
    signal addRequested(string presetId)
    signal closeRequested()

    property string category: ""   // empty shows everything

    readonly property var shown: {
        var all = app.presets
        if (category === "")
            return all
        var some = []
        for (var i = 0; i < all.length; ++i)
            if (all[i].category === category)
                some.push(all[i])
        return some
    }

    color: "#c8000000"

    // Nothing behind the library can be clicked or scrolled.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
        onClicked: root.closeRequested()
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 1240)
        height: parent.height - 48
        radius: 10
        color: theme.panel
        border.color: theme.line

        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        Text {
            id: title
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 24
            anchors.topMargin: 18
            text: "Effect library"
            color: theme.text
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Text {
            anchors.left: title.right
            anchors.leftMargin: 14
            anchors.right: closeButton.left
            anchors.rightMargin: 12
            anchors.baseline: title.baseline
            text: "Pick a starting point. Every layer and control in it can be changed afterwards."
            color: theme.dim
            font.pixelSize: theme.fontSize
            elide: Text.ElideRight
        }
        VButton {
            id: closeButton
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 16
            anchors.topMargin: 14
            text: "Close"
            tip: "Back to the effect (Esc)"
            onClicked: root.closeRequested()
        }

        Flow {
            id: chips
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: title.bottom
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            anchors.topMargin: 14
            spacing: 6

            VButton {
                text: "All"
                checked: root.category === ""
                onClicked: root.category = ""
            }
            Repeater {
                model: app.presetCategories
                delegate: VButton {
                    required property string modelData
                    text: modelData
                    checked: root.category === modelData
                    onClicked: root.category = modelData
                }
            }
        }

        GridView {
            id: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: chips.bottom
            anchors.bottom: footer.top
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.topMargin: 14
            anchors.bottomMargin: 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 0

            readonly property int columns: Math.max(2, Math.floor(width / 232))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.floor(cellWidth * 0.66) + 62
            model: root.shown

            delegate: Item {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 6
                    radius: 8
                    color: hover.hovered ? theme.hover : theme.raised
                    border.width: 1
                    border.color: hover.hovered ? theme.accent : theme.line

                    // True while the pointer is anywhere on the card,
                    // including over the button that appears on it.
                    HoverHandler { id: hover }

                    Rectangle {
                        id: stage
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        height: parent.height - 62
                        radius: 5
                        color: theme.viewport

                        PresetPreview {
                            id: preview
                            anchors.fill: parent
                            preset: cell.modelData.id
                        }
                        // Each card plays only while the library is showing.
                        FrameAnimation {
                            running: root.visible
                            onTriggered: preview.advance(frameTime)
                        }
                    }

                    Text {
                        id: cardName
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: stage.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.topMargin: 7
                        text: cell.modelData.name
                        color: theme.text
                        font.pixelSize: theme.fontSize
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        anchors.left: cardName.left
                        anchors.right: cardName.right
                        anchors.top: cardName.bottom
                        anchors.topMargin: 2
                        text: cell.modelData.description
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.openRequested(cell.modelData.id)
                    }

                    VButton {
                        visible: hover.hovered
                        anchors.right: stage.right
                        anchors.bottom: stage.bottom
                        anchors.margins: 6
                        text: "Add to my effect"
                        onClicked: root.addRequested(cell.modelData.id)
                    }
                }
            }
        }

        Text {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            anchors.bottomMargin: 12
            text: "Click a card to open it as a new effect. “Add to my effect” puts its layers into the effect you already have."
            color: theme.faint
            font.pixelSize: theme.smallFontSize
            elide: Text.ElideRight
        }
    }
}
