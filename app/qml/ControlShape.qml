import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import VfxForge 1.0

// What each particle looks like: one of the built-in shapes, or the artist's
// own picture or sprite sheet.
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

    implicitHeight: column.implicitHeight

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

        // ---- the built-in shapes
        Text {
            visible: root.picture.has
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Shapes (used when there is no picture):"
            color: theme.faint
            font.pixelSize: theme.smallFontSize
        }
        Flow {
            width: parent.width
            spacing: 4
            opacity: root.picture.has ? 0.45 : 1.0

            Repeater {
                model: root.info.options
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
                            root.value = tile.modelData.value
                            app.setControlChoice(root.controlIndex, tile.modelData.value)
                        }
                    }
                    ToolTip.visible: tileArea.containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: tile.modelData.label
                }
            }
        }
    }
}
