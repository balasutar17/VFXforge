import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import VfxForge 1.0

// What each particle looks like: a shape from the shape library, or the
// artist's own picture or sprite sheet.
Item {
    id: root

    property var info
    property int controlIndex: -1
    property string value: info.value

    // The picture settings, kept here so the panel answers at once.
    readonly property var picture: info.picture || ({ has: false })
    property int columns: picture.columns || 1
    property int rows: picture.rows || 1
    property int frames: picture.frames || 0
    property string animate: picture.animate || "life"
    property real fps: picture.fps || 12
    property bool randomStart: picture.randomStart || false
    readonly property int cells: Math.max(1, columns * rows)
    readonly property int pictures: frames > 0 ? Math.min(frames, cells) : cells

    // The shapes are sorted into groups, one of which shows at a time:
    // to begin with, the one the layer's own shape is in. Typing in the
    // search box looks through every group instead.
    property string category: ""
    property string find: ""
    readonly property bool finding: find.trim().length > 0
    readonly property var shown: {
        var out = []
        var words = find.trim().toLowerCase()
        for (var i = 0; i < info.options.length; ++i) {
            var o = info.options[i]
            if (words.length > 0
                    ? (o.label.toLowerCase().indexOf(words) >= 0 || o.value.indexOf(words) >= 0)
                    : o.category === category)
                out.push(o)
        }
        return out
    }

    // Some shapes have settings of their own: how many points, how thick,
    // how pointed, how round. `edits` is the chosen shape's (none, for most
    // shapes); `editValues` is where each stands, 0 being the shape as drawn.
    readonly property var edits: {
        for (var i = 0; i < info.options.length; ++i)
            if (info.options[i].value === value)
                return info.options[i].edits || []
        return []
    }
    property var editValues: ({
        shapeCount: picture.shapeCount || 0,
        shapeThickness: picture.shapeThickness || 0,
        shapeSharpness: picture.shapeSharpness || 0,
        shapeRound: picture.shapeRound || 0
    })
    readonly property bool edited: {
        for (var i = 0; i < edits.length; ++i) {
            var v = editValues[edits[i].key] || 0
            if (v !== 0 && !(edits[i].whole && v === edits[i].own))
                return true
        }
        return false
    }
    function noEdits() {
        return { shapeCount: 0, shapeThickness: 0, shapeSharpness: 0, shapeRound: 0 }
    }
    // What a slider shows: a count of 0 means the shape's own count.
    function editShown(edit) {
        var v = editValues[edit.key] || 0
        return edit.whole && v === 0 ? edit.own : v
    }
    function setEdit(edit, v) {
        v = Math.max(edit.from, Math.min(edit.to, v))
        // Amounts move in the 63 steps each way that shapes are drawn at.
        v = edit.whole ? Math.round(v) : Math.round(v * 63) / 63
        if (v === editShown(edit))
            return
        var next = noEdits()
        for (var key in next)
            next[key] = editValues[key] || 0
        next[edit.key] = v
        editValues = next
        app.setPictureNumber(edit.key, v)
    }
    function resetEdits() {
        app.beginEdit("Shape As Drawn")
        for (var i = 0; i < edits.length; ++i)
            if ((editValues[edits[i].key] || 0) !== 0)
                app.setPictureNumber(edits[i].key, 0)
        app.endEdit()
        editValues = noEdits()
    }

    implicitHeight: column.implicitHeight

    Component.onCompleted: {
        for (var i = 0; i < info.options.length; ++i)
            if (info.options[i].value === value)
                category = info.options[i].category
        if (category === "" && info.categories && info.categories.length > 0)
            category = info.categories[0]
    }

    function labelOf(value) {
        for (var i = 0; i < info.options.length; ++i)
            if (info.options[i].value === value)
                return info.options[i].label
        return value
    }

    FileDialog {
        id: pictureDialog
        title: "Choose a picture or sprite sheet for this layer"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Pictures (*.png *.webp *.tga *.tif *.tiff *.jpg *.jpeg *.bmp *.gif)", "All files (*)"]
        onAccepted: app.usePicture(selectedFile)
    }

    Column {
        id: column
        width: parent.width
        spacing: 6

        ControlHeader {
            width: parent.width
            label: root.info.label
            unit: root.picture.has ? "Your picture" : root.labelOf(root.value)
        }

        // ---- the artist's own picture
        Rectangle {
            width: parent.width
            height: pictureColumn.implicitHeight + 16
            radius: 6
            color: theme.field
            border.width: 1
            border.color: root.picture.has ? theme.accent : theme.line

            Column {
                id: pictureColumn
                x: 8
                y: 8
                width: parent.width - 16
                spacing: 8

                // Nothing chosen yet.
                Column {
                    visible: !root.picture.has
                    width: parent.width
                    spacing: 6
                    Flow {
                        width: parent.width
                        spacing: 6
                        VButton {
                            text: "Use my own picture…"
                            primary: true
                            tip: "Draw a picture you painted on every particle. A sprite sheet plays as an animation."
                            onClicked: pictureDialog.open()
                        }
                        VButton {
                            text: "Try a sample sheet"
                            quiet: true
                            tip: "A hand-drawn toon flame in 8 frames, to see how sprite sheets work."
                            onClicked: app.useSamplePicture()
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "A PNG with a see-through background works best. Name a sheet like fire_4x2.png and its grid is set for you."
                        color: theme.faint
                        font.pixelSize: theme.smallFontSize
                    }
                }

                // A picture is in use.
                Row {
                    visible: root.picture.has
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        id: thumb
                        width: 72
                        height: 72
                        radius: 4
                        color: "#2a2e38"
                        clip: true

                        // A checkerboard shows what is see-through.
                        Grid {
                            anchors.fill: parent
                            columns: 6
                            Repeater {
                                model: 36
                                Rectangle {
                                    required property int index
                                    width: 12
                                    height: 12
                                    color: ((index % 6) + Math.floor(index / 6)) % 2 ? "#363b47" : "#2a2e38"
                                }
                            }
                        }
                        Image {
                            id: thumbImage
                            anchors.fill: parent
                            anchors.margins: 2
                            source: root.picture.has ? root.picture.source : ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            sourceSize.width: 256
                            sourceSize.height: 256
                        }
                        // The sprite-sheet grid over the picture.
                        Item {
                            x: thumbImage.x + (thumbImage.width - thumbImage.paintedWidth) / 2
                            y: thumbImage.y + (thumbImage.height - thumbImage.paintedHeight) / 2
                            width: thumbImage.paintedWidth
                            height: thumbImage.paintedHeight
                            Repeater {
                                model: Math.max(0, root.columns - 1)
                                Rectangle {
                                    required property int index
                                    x: (index + 1) * parent.width / root.columns
                                    width: 1
                                    height: parent.height
                                    color: theme.accent
                                    opacity: 0.8
                                }
                            }
                            Repeater {
                                model: Math.max(0, root.rows - 1)
                                Rectangle {
                                    required property int index
                                    y: (index + 1) * parent.height / root.rows
                                    height: 1
                                    width: parent.width
                                    color: theme.accent
                                    opacity: 0.8
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width - thumb.width - 10
                        spacing: 4
                        Text {
                            width: parent.width
                            text: root.picture.name || ""
                            elide: Text.ElideMiddle
                            color: theme.text
                            font.pixelSize: theme.fontSize
                        }
                        Text {
                            width: parent.width
                            text: root.picture.problem
                                  ? root.picture.problem
                                  : (root.picture.width + " × " + root.picture.height + " px · "
                                     + (root.pictures === 1 ? "1 picture" : root.pictures + " pictures"))
                            wrapMode: Text.WordWrap
                            color: root.picture.problem ? theme.error : theme.faint
                            font.pixelSize: theme.smallFontSize
                        }
                        Flow {
                            width: parent.width
                            spacing: 6
                            VButton {
                                text: "Change…"
                                onClicked: pictureDialog.open()
                            }
                            VButton {
                                text: "Use shape"
                                quiet: true
                                tip: "Stop using the picture and draw the chosen shape again."
                                onClicked: app.clearPicture()
                            }
                        }
                    }
                }

                // Sprite-sheet settings.
                Column {
                    visible: root.picture.has
                    width: parent.width
                    spacing: 8

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Color tints the picture: white shows it as painted."
                        color: theme.faint
                        font.pixelSize: theme.smallFontSize
                    }
                    Row {
                        spacing: 10
                        Repeater {
                            model: [
                                { key: "columns", label: "Across", tip: "How many pictures across the sheet." },
                                { key: "rows", label: "Down", tip: "How many pictures down the sheet." },
                                { key: "frames", label: "Use", tip: "How many of the pictures to play, left to right and top to bottom. 0 plays them all." }
                            ]
                            delegate: Column {
                                required property var modelData
                                spacing: 2
                                Text {
                                    text: modelData.label
                                    color: theme.faint
                                    font.pixelSize: theme.smallFontSize
                                }
                                VNumberField {
                                    width: 56
                                    decimals: 0
                                    value: modelData.key === "columns" ? root.columns
                                         : modelData.key === "rows" ? root.rows : root.frames
                                    onEdited: function(v) {
                                        var lo = modelData.key === "frames" ? 0 : 1
                                        var hi = modelData.key === "frames" ? 4096 : 64
                                        var n = Math.max(lo, Math.min(hi, Math.round(v)))
                                        if (modelData.key === "columns") root.columns = n
                                        else if (modelData.key === "rows") root.rows = n
                                        else root.frames = n
                                        app.setPictureNumber(modelData.key, n)
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 4
                        visible: root.pictures > 1
                        Text {
                            text: "Play the pictures"
                            color: theme.faint
                            font.pixelSize: theme.smallFontSize
                        }
                        Flow {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: [
                                    { value: "life", label: "Once per life", tip: "Each particle plays the sheet once, from birth to death." },
                                    { value: "loop", label: "Loop", tip: "Each particle plays the sheet over and over at the frame rate." },
                                    { value: "random", label: "One at random", tip: "Each particle shows one picture from the sheet, picked at random." }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    tip: modelData.tip
                                    checked: root.animate === modelData.value
                                    onClicked: {
                                        root.animate = modelData.value
                                        app.setPictureChoice("animate", modelData.value)
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 4
                        visible: root.pictures > 1 && root.animate === "loop"
                        ControlHeader {
                            width: parent.width
                            label: "Frame rate"
                            unit: "per second"
                        }
                        Item {
                            width: parent.width
                            height: 24
                            VSlider {
                                anchors.left: parent.left
                                anchors.right: fpsBox.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                from: 1
                                to: 60
                                value: root.fps
                                onDragStarted: app.beginEdit("Change Frame Rate")
                                onDragEnded: app.endEdit()
                                onMoved: function(v) {
                                    root.fps = Math.round(v)
                                    app.setPictureNumber("fps", root.fps)
                                }
                            }
                            VNumberField {
                                id: fpsBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                value: root.fps
                                decimals: 0
                                onEdited: function(v) {
                                    root.fps = Math.max(0.1, Math.min(240, v))
                                    app.setPictureNumber("fps", root.fps)
                                }
                            }
                        }
                        VCheck {
                            text: "Start each particle on a different picture"
                            checked: root.randomStart
                            onToggled: function(on) {
                                root.randomStart = on
                                app.setPictureFlag("randomStart", on)
                            }
                        }
                    }
                }
            }
        }

        // ---- the shapes
        Text {
            visible: root.picture.has
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Shapes (used when there is no picture):"
            color: theme.faint
            font.pixelSize: theme.smallFontSize
        }
        Column {
            width: parent.width
            spacing: 6
            opacity: root.picture.has ? 0.45 : 1.0

            // Find a shape by its name, in every group at once.
            Rectangle {
                width: parent.width
                height: 26
                radius: 4
                color: theme.field
                border.width: 1
                border.color: findInput.activeFocus ? theme.accent : theme.line

                TextInput {
                    id: findInput
                    objectName: "shapeFind"
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 26
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.text
                    selectionColor: theme.accent
                    selectedTextColor: theme.accentText
                    font.pixelSize: theme.fontSize
                    selectByMouse: true
                    clip: true
                    maximumLength: 40
                    onTextChanged: root.find = text
                    Keys.onEscapePressed: {
                        text = ""
                        focus = false
                    }
                    Keys.onReturnPressed: focus = false
                    Keys.onEnterPressed: focus = false
                }
                Text {
                    anchors.fill: findInput
                    verticalAlignment: Text.AlignVCenter
                    visible: findInput.text.length === 0 && !findInput.activeFocus
                    text: "Find a shape… (" + root.info.options.length + " to choose from)"
                    color: theme.faint
                    font.pixelSize: theme.fontSize
                    elide: Text.ElideRight
                }
                Text {
                    visible: findInput.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    color: clearArea.containsMouse ? theme.text : theme.faint
                    font.pixelSize: theme.fontSize + 3
                    MouseArea {
                        id: clearArea
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        onClicked: findInput.text = ""
                    }
                }
            }

            // The groups the shapes are sorted into.
            Flow {
                width: parent.width
                spacing: 4
                visible: !root.finding

                Repeater {
                    model: root.info.categories || []
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool chosen: root.category === modelData

                        width: chipLabel.implicitWidth + 16
                        height: 22
                        radius: 11
                        color: chosen ? theme.accent : (chipArea.containsMouse ? theme.hover : theme.raised)

                        Text {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: chip.modelData
                            color: chip.chosen ? theme.accentText : theme.text
                            font.pixelSize: theme.smallFontSize
                        }
                        MouseArea {
                            id: chipArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.category = chip.modelData
                        }
                    }
                }
            }
            Text {
                visible: root.finding
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.shown.length === 0 ? "No shape has that in its name."
                    : root.shown.length === 1 ? "1 shape"
                    : root.shown.length + " shapes"
                color: theme.faint
                font.pixelSize: theme.smallFontSize
            }

            Flow {
                width: parent.width
                spacing: 4

                Repeater {
                    model: root.shown
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        readonly property bool chosen: root.value === modelData.value

                        width: 38
                        height: 38
                        radius: 5
                        color: chosen ? theme.hover : (tileArea.containsMouse ? theme.raised : theme.field)
                        border.width: 1
                        border.color: chosen ? theme.accent : theme.line

                        ShapeIcon {
                            anchors.fill: parent
                            anchors.margins: 5
                            shape: tile.modelData.value
                            color: tile.chosen ? theme.accent : "#c9ced8"
                        }
                        MouseArea {
                            id: tileArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                // A newly picked shape comes as it was drawn.
                                root.value = tile.modelData.value
                                root.editValues = root.noEdits()
                                app.chooseShape(root.controlIndex, tile.modelData.value)
                            }
                        }
                        ToolTip.visible: tileArea.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: tile.modelData.label
                    }
                }
            }

            // ---- the chosen shape's own settings
            Column {
                visible: root.edits.length > 0
                width: parent.width
                spacing: 6
                topPadding: 4

                Item {
                    width: parent.width
                    height: 24
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Change this shape"
                        color: theme.faint
                        font.pixelSize: theme.smallFontSize
                    }
                    VButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 22
                        visible: root.edited
                        quiet: true
                        text: "As drawn"
                        tip: "Put the shape back the way it was drawn."
                        onClicked: root.resetEdits()
                    }
                }

                Repeater {
                    model: root.edits
                    delegate: Item {
                        id: editRow
                        required property var modelData
                        width: parent.width
                        height: 24

                        Text {
                            id: editLabel
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 76
                            text: editRow.modelData.label
                            color: theme.text
                            font.pixelSize: theme.fontSize
                            elide: Text.ElideRight
                        }
                        VSlider {
                            anchors.left: editLabel.right
                            anchors.right: editBox.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            from: editRow.modelData.from
                            to: editRow.modelData.to
                            value: root.editShown(editRow.modelData)
                            onDragStarted: app.beginEdit("Change Shape " + editRow.modelData.label)
                            onDragEnded: app.endEdit()
                            onMoved: function(v) { root.setEdit(editRow.modelData, v) }
                        }
                        VNumberField {
                            id: editBox
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 52
                            value: root.editShown(editRow.modelData)
                            decimals: editRow.modelData.whole ? 0 : 2
                            onEdited: function(v) { root.setEdit(editRow.modelData, v) }
                        }
                        MouseArea {
                            id: editHover
                            anchors.fill: editLabel
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                        ToolTip.visible: editHover.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: editRow.modelData.tip
                    }
                }
            }
        }
    }
}
