import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import VfxForge 1.0

// The Shader tab: choose a shader family and preset, change its controls, watch it animate,
// and send a working material to Unity.
Rectangle {
    id: root

    signal closeRequested()

    property bool advanced: false
    property string tag: "All"
    property string search: ""
    property string paramSearch: ""
    readonly property var backgrounds: ["Dark", "Grey", "Light", "Checker"]

    color: theme.window

    // Nothing behind the workspace can be clicked or scrolled.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    FileDialog {
        id: openDialog
        title: "Open a shader"
        fileMode: FileDialog.OpenFile
        nameFilters: ["VFX Forge shaders (*.vfxshader)", "All files (*)"]
        onAccepted: shaderStudio.openFile(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: "Save the shader"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "vfxshader"
        nameFilters: ["VFX Forge shaders (*.vfxshader)"]
        onAccepted: shaderStudio.saveAs(selectedFile)
    }
    FolderDialog {
        id: exportDialog
        title: "Choose your Unity project's folder"
        onAccepted: shaderStudio.exportToUnity(selectedFolder)
    }
    FileDialog {
        id: modelDialog
        title: "Choose a model to preview on"
        fileMode: FileDialog.OpenFile
        nameFilters: ["3D models (*.obj *.glb *.gltf)", "All files (*)"]
        onAccepted: shaderStudio.loadPreviewModel(selectedFile)
    }

    component Heading: Text {
        color: theme.dim
        font.pixelSize: theme.smallFontSize
        font.letterSpacing: 1.1
        font.weight: Font.DemiBold
    }

    // A one-line box that reports every keystroke (the library and the controls filter as you type).
    component SearchBox: Rectangle {
        id: box
        property string text: ""
        property string placeholder: "Search"
        implicitHeight: 26
        radius: 4
        color: theme.field
        border.width: 1
        border.color: field.activeFocus ? theme.accent : theme.line
        TextInput {
            id: field
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
            onTextChanged: box.text = text
        }
        Text {
            anchors.fill: parent
            anchors.leftMargin: 7
            verticalAlignment: Text.AlignVCenter
            visible: field.text.length === 0
            text: box.placeholder
            color: theme.faint
            font.pixelSize: theme.fontSize
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------ top bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 12
                spacing: 6

                VButton { text: "← Effects"; tip: "Back to the effect editor"; onClicked: root.closeRequested() }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: theme.line }
                VButton { text: "New"; tip: "Start again from this family's defaults"; onClicked: shaderStudio.newShader() }
                VButton { text: "Open…"; tip: "Open a saved .vfxshader file"; onClicked: openDialog.open() }
                VButton {
                    text: "Save"
                    tip: "Save this shader"
                    enabled: shaderStudio.dirty || !shaderStudio.hasFile
                    onClicked: { if (shaderStudio.hasFile) shaderStudio.save(); else saveDialog.open() }
                }
                VButton { text: "Save As…"; tip: "Save a copy under a new name"; onClicked: saveDialog.open() }
                VButton { text: "Undo"; tip: "Undo the last change"; enabled: shaderStudio.canUndo; onClicked: shaderStudio.undo() }
                VButton { text: "Redo"; tip: "Redo"; enabled: shaderStudio.canRedo; onClicked: shaderStudio.redo() }
                VButton { text: "Duplicate"; tip: "Make a copy to take in another direction"; onClicked: shaderStudio.duplicate() }
                VButton {
                    text: "Compare"
                    checked: shaderStudio.compareOn
                    tip: "Show the pinned look beside the shader you are editing"
                    onClicked: shaderStudio.compareOn = !shaderStudio.compareOn
                }
                VButton { text: "Pin"; tip: "Remember the current look to compare against"; onClicked: shaderStudio.pinForCompare() }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: theme.line }
                Repeater {
                    model: shaderStudio.qualityNames()
                    delegate: VButton {
                        required property string modelData
                        required property int index
                        text: modelData === "Standard" ? "Std" : modelData === "Cinematic" ? "Cine" : modelData
                        checked: shaderStudio.quality === index
                        tip: shaderStudio.qualityNote(index)
                        onClicked: shaderStudio.quality = index
                    }
                }
                Item { Layout.fillWidth: true }
                VButton { text: "Check"; tip: "Look for problems before exporting"; onClicked: shaderStudio.validate() }
                VButton {
                    text: "Export to Unity…"
                    primary: true
                    tip: "Write the shader, material, animation and scripts into a Unity project"
                    onClicked: exportDialog.open()
                }
            }
        }

        // ------------------------------------------------------------ the three panels
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- library
            Rectangle {
                Layout.preferredWidth: 292
                Layout.fillHeight: true
                color: theme.panel
                border.color: theme.line
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Heading { text: "SHADER LIBRARY" }
                    SearchBox {
                        Layout.fillWidth: true
                        placeholder: "Search shaders and presets"
                        onTextChanged: root.search = text
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: shaderStudio.tags()
                            delegate: VButton {
                                required property string modelData
                                text: modelData
                                implicitHeight: 22
                                checked: root.tag === modelData
                                onClicked: root.tag = modelData
                            }
                        }
                    }

                    ListView {
                        id: libraryList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        boundsBehavior: Flickable.StopAtBounds
                        model: { shaderStudio.libraryRevision; return shaderStudio.library(root.search, root.tag) }
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Column {
                            id: fam
                            required property var modelData
                            width: libraryList.width - 8
                            spacing: 4

                            Text {
                                width: parent.width
                                text: fam.modelData.number + "  " + fam.modelData.name
                                color: theme.text
                                font.pixelSize: theme.fontSize
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                text: fam.modelData.summary
                                color: theme.dim
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: fam.modelData.presets
                                delegate: Rectangle {
                                    id: card
                                    required property var modelData
                                    readonly property bool current: shaderStudio.familyId === modelData.family && shaderStudio.presetId === modelData.id
                                    width: fam.width
                                    height: 54
                                    radius: 5
                                    color: current ? theme.hover : cardArea.containsMouse ? theme.raised : "transparent"
                                    border.width: current ? 1 : 0
                                    border.color: theme.accent

                                    ShaderThumb {
                                        x: 4
                                        y: 4
                                        width: 46
                                        height: 46
                                        family: card.modelData.family
                                        preset: card.modelData.id
                                    }
                                    Column {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 58
                                        anchors.right: star.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 2
                                        Text {
                                            width: parent.width
                                            text: card.modelData.name
                                            color: theme.text
                                            font.pixelSize: theme.fontSize
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            text: card.modelData.description
                                            color: theme.dim
                                            font.pixelSize: theme.smallFontSize
                                            elide: Text.ElideRight
                                        }
                                    }
                                    MouseArea {
                                        id: cardArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: shaderStudio.loadPreset(card.modelData.family, card.modelData.id)
                                    }
                                    Text {
                                        id: star
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 20
                                        horizontalAlignment: Text.AlignHCenter
                                        text: card.modelData.favorite ? "★" : "☆"
                                        color: card.modelData.favorite ? theme.accent : theme.faint
                                        font.pixelSize: 16
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            onClicked: shaderStudio.toggleFavorite(card.modelData.key)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- the artist's own presets
                    Heading { text: "MY PRESETS" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        SearchBox {
                            id: presetName
                            Layout.fillWidth: true
                            placeholder: "Name to save this look as"
                        }
                        VButton {
                            text: "Save"
                            tip: "Keep the current look in your own presets"
                            enabled: presetName.text.trim().length > 0
                            onClicked: shaderStudio.saveUserPreset(presetName.text)
                        }
                    }
                    ListView {
                        id: myList
                        visible: count > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: count > 0 ? Math.min(110, count * 28) : 0
                        clip: true
                        model: { shaderStudio.libraryRevision; return shaderStudio.userPresets() }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            id: mine
                            required property var modelData
                            width: myList.width - 8
                            height: 26
                            Text {
                                anchors.left: parent.left
                                anchors.right: del.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: mine.modelData.name + (mine.modelData.family.length > 0 ? "  ·  " + mine.modelData.family : "")
                                color: myArea.containsMouse ? theme.accent : theme.text
                                font.pixelSize: theme.fontSize
                                elide: Text.ElideRight
                                MouseArea {
                                    id: myArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: shaderStudio.loadUserPreset(mine.modelData.name)
                                }
                            }
                            VButton {
                                id: del
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: "×"
                                quiet: true
                                implicitHeight: 22
                                tip: "Delete this preset"
                                onClicked: shaderStudio.deleteUserPreset(mine.modelData.name)
                            }
                        }
                    }
                }
            }

            // ---- preview
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.viewport

                ShaderPreview {
                    id: preview
                    anchors.fill: parent
                    studio: shaderStudio
                }

                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        shaderStudio.yaw = shaderStudio.yaw - (mouse.x - lastX) * 0.01
                        shaderStudio.pitch = shaderStudio.pitch + (mouse.y - lastY) * 0.01
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onWheel: function(wheel) {
                        shaderStudio.zoom = shaderStudio.zoom * (wheel.angleDelta.y > 0 ? 1.08 : 0.92)
                        wheel.accepted = true
                    }
                }

                // Shape buttons, top left.
                Flow {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 4
                    Repeater {
                        model: shaderStudio.shapes()
                        delegate: VButton {
                            required property string modelData
                            text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                            implicitHeight: 24
                            checked: shaderStudio.shape === modelData
                            tip: "Preview on this shape. The exported material works on any mesh."
                            onClicked: shaderStudio.shape = modelData
                        }
                    }
                    VButton {
                        text: shaderStudio.hasModel ? "Remove model" : "Own model…"
                        implicitHeight: 24
                        tip: "Preview on your own .obj / .glb / .gltf"
                        onClicked: { if (shaderStudio.hasModel) shaderStudio.clearPreviewModel(); else modelDialog.open() }
                    }
                }

                Text {
                    visible: shaderStudio.compareOn
                    anchors.top: parent.top
                    anchors.topMargin: 44
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    text: "PINNED"
                    color: theme.dim
                    font.pixelSize: theme.smallFontSize
                    font.letterSpacing: 1.1
                }
                Text {
                    visible: shaderStudio.compareOn
                    anchors.top: parent.top
                    anchors.topMargin: 44
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    text: "EDITING"
                    color: theme.accent
                    font.pixelSize: theme.smallFontSize
                    font.letterSpacing: 1.1
                }

                // Background, bloom and camera, bottom.
                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 4
                    Repeater {
                        model: root.backgrounds
                        delegate: VButton {
                            required property string modelData
                            required property int index
                            text: modelData
                            implicitHeight: 24
                            checked: shaderStudio.background === index
                            onClicked: shaderStudio.background = index
                        }
                    }
                    VCheck {
                        text: "Bloom"
                        checked: shaderStudio.bloom
                        onToggled: function(now) { shaderStudio.bloom = now }
                    }
                    VButton { text: "Reset view"; implicitHeight: 24; tip: "Drag to orbit, scroll to zoom"; onClicked: shaderStudio.resetCamera() }
                    VButton {
                        visible: shaderStudio.hasHit()
                        text: "Hit now"
                        implicitHeight: 24
                        tip: "Send an impact to the shield, as a gameplay hit would"
                        onClicked: shaderStudio.triggerHit()
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "Preview " + Math.round(shaderStudio.renderMs) + " ms · CPU, shared shader source"
                        color: theme.faint
                        font.pixelSize: theme.smallFontSize
                    }
                }
            }

            // ---- parameters
            Rectangle {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                color: theme.panel
                border.color: theme.line
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Heading { text: "PARAMETERS" }
                    VTextField {
                        Layout.fillWidth: true
                        text: shaderStudio.name
                        onEdited: function(newText) { shaderStudio.name = newText }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: shaderStudio.familyName + " — " + shaderStudio.familySummary
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        VButton { text: "Basic"; checked: !root.advanced; onClicked: root.advanced = false }
                        VButton { text: "Advanced"; checked: root.advanced; onClicked: root.advanced = true }
                        Item { Layout.fillWidth: true }
                        VButton { text: "Additive"; checked: shaderStudio.blend === "additive"; tip: "Adds light: glows and energy"; onClicked: shaderStudio.blend = "additive" }
                        VButton { text: "Alpha"; checked: shaderStudio.blend === "alpha"; tip: "Normal transparency: smoke and solid looks"; onClicked: shaderStudio.blend = "alpha" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        SearchBox {
                            Layout.fillWidth: true
                            placeholder: "Search parameters"
                            onTextChanged: root.paramSearch = text
                        }
                        VButton { text: "−30°"; tip: "A colour variant: turn every colour round"; onClicked: { shaderStudio.checkpoint(); shaderStudio.colorVariant(-30) } }
                        VButton { text: "+30°"; tip: "A colour variant: turn every colour round"; onClicked: { shaderStudio.checkpoint(); shaderStudio.colorVariant(30) } }
                        VButton { text: "Reset all"; tip: "Back to the preset's own values"; onClicked: shaderStudio.resetAll() }
                    }

                    Flickable {
                        id: paramFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: paramColumn.implicitHeight + 12
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        Column {
                            id: paramColumn
                            width: paramFlick.width - 10
                            spacing: 10

                            Repeater {
                                model: { shaderStudio.structureRevision; return shaderStudio.controls(root.paramSearch, root.advanced) }
                                delegate: Item {
                                    id: ctl
                                    required property var modelData
                                    readonly property int idx: modelData.index
                                    readonly property bool isNumber: modelData.kind === "number"
                                    readonly property bool isColor: modelData.kind === "color"
                                    readonly property bool isChoice: modelData.kind === "choice"
                                    property bool changedFromDefault: { shaderStudio.revision; return !shaderStudio.isDefault(idx) }
                                    property bool animated: { shaderStudio.revision; return shaderStudio.isAnimated(idx) }
                                    property bool keyHere: { shaderStudio.revision; shaderStudio.timelineTime; return shaderStudio.hasKeyHere(idx) }
                                    width: paramColumn.width
                                    height: head.height + body.height + 4

                                    // Colour state kept here: a grey has no hue of its own.
                                    property real hue: 0
                                    property real sat: 0
                                    property real val: 1
                                    property real boost: 1
                                    property bool dragging: false
                                    function syncColor() {
                                        if (!isColor || dragging)
                                            return
                                        var c = shaderStudio.colorInfo(idx)
                                        if (c.s > 0.01 || Math.abs(hue - c.h) > 0.5)
                                            hue = c.h
                                        sat = c.s
                                        val = c.v
                                        boost = c.boost
                                    }
                                    Component.onCompleted: syncColor()
                                    Connections {
                                        target: shaderStudio
                                        function onChanged() { ctl.syncColor() }
                                    }
                                    function pushColor() { shaderStudio.setColorHsv(idx, hue, sat, val, boost) }

                                    Item {
                                        id: head
                                        width: parent.width
                                        height: 22
                                        Text {
                                            id: labelText
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: ctl.modelData.label
                                            color: ctl.animated ? theme.accent : theme.text
                                            font.pixelSize: theme.fontSize
                                            font.weight: Font.DemiBold
                                            MouseArea {
                                                id: labelArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                            }
                                            ToolTip.visible: labelArea.containsMouse
                                            ToolTip.delay: 500
                                            ToolTip.text: ctl.modelData.tip
                                        }
                                        Row {
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2
                                            VButton {
                                                visible: ctl.isNumber
                                                text: "◆"
                                                quiet: true
                                                implicitWidth: 24
                                                implicitHeight: 22
                                                tip: ctl.keyHere ? "Remove the keyframe at the playhead" : "Add a keyframe at the playhead: this control then follows the timeline"
                                                onClicked: shaderStudio.toggleKey(ctl.idx)
                                                opacity: ctl.keyHere || ctl.animated ? 1.0 : 0.55
                                            }
                                            VButton {
                                                visible: ctl.changedFromDefault
                                                text: "↺"
                                                quiet: true
                                                implicitWidth: 24
                                                implicitHeight: 22
                                                tip: "Back to the default"
                                                onClicked: shaderStudio.resetControl(ctl.idx)
                                            }
                                        }
                                    }

                                    Item {
                                        id: body
                                        anchors.top: head.bottom
                                        width: parent.width
                                        height: ctl.isColor ? 108 : ctl.isChoice ? choiceFlow.height : 24

                                        // number
                                        RowLayout {
                                            visible: ctl.isNumber
                                            anchors.fill: parent
                                            spacing: 8
                                            VSlider {
                                                Layout.fillWidth: true
                                                from: ctl.modelData.min
                                                to: ctl.modelData.max
                                                value: { shaderStudio.revision; if (ctl.animated) shaderStudio.timelineTime; return shaderStudio.number(ctl.idx) }
                                                onDragStarted: shaderStudio.checkpoint()
                                                onMoved: function(v) { shaderStudio.setNumber(ctl.idx, v) }
                                            }
                                            VNumberField {
                                                Layout.preferredWidth: 62
                                                value: { shaderStudio.revision; if (ctl.animated) shaderStudio.timelineTime; return shaderStudio.number(ctl.idx) }
                                                decimals: ctl.modelData.max - ctl.modelData.min > 50 ? 1 : 2
                                                onEdited: function(v) { shaderStudio.checkpoint(); shaderStudio.setNumber(ctl.idx, v) }
                                            }
                                        }

                                        // colour: hue, saturation, brightness, and a boost for HDR glow
                                        Column {
                                            visible: ctl.isColor
                                            anchors.fill: parent
                                            spacing: 2
                                            Rectangle {
                                                width: parent.width
                                                height: 14
                                                radius: 3
                                                color: { shaderStudio.revision; return shaderStudio.colorShown(ctl.idx) }
                                                border.color: theme.line
                                            }
                                            Repeater {
                                                model: [
                                                    { name: "Hue", from: 0, to: 0.999, key: "hue" },
                                                    { name: "Saturation", from: 0, to: 1, key: "sat" },
                                                    { name: "Brightness", from: 0, to: 1, key: "val" },
                                                    { name: "Glow ×", from: 1, to: 16, key: "boost" }
                                                ]
                                                delegate: RowLayout {
                                                    id: crow
                                                    required property var modelData
                                                    width: parent.width
                                                    height: 20
                                                    spacing: 6
                                                    Text {
                                                        Layout.preferredWidth: 66
                                                        text: crow.modelData.name
                                                        color: theme.dim
                                                        font.pixelSize: theme.smallFontSize
                                                    }
                                                    VSlider {
                                                        Layout.fillWidth: true
                                                        from: crow.modelData.from
                                                        to: crow.modelData.to
                                                        value: ctl[crow.modelData.key]
                                                        onDragStarted: { shaderStudio.checkpoint(); ctl.dragging = true }
                                                        onDragEnded: ctl.dragging = false
                                                        onMoved: function(v) {
                                                            ctl[crow.modelData.key] = v
                                                            ctl.pushColor()
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        // choice
                                        Flow {
                                            id: choiceFlow
                                            visible: ctl.isChoice
                                            width: parent.width
                                            spacing: 4
                                            Repeater {
                                                model: ctl.modelData.choices
                                                delegate: VButton {
                                                    required property string modelData
                                                    required property int index
                                                    text: modelData
                                                    checked: { shaderStudio.revision; return Math.round(shaderStudio.number(ctl.idx)) === index }
                                                    onClicked: shaderStudio.setChoice(ctl.idx, index)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                visible: shaderStudio.familyLimits.length > 0
                                text: "What this shader does not do:\n• " + shaderStudio.familyLimits.join("\n• ")
                                color: theme.faint
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------ timeline
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 236
            color: theme.panel
            border.color: theme.line
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Heading { text: "TIMELINE" }
                    VButton {
                        text: shaderStudio.playing ? "Pause" : "Play"
                        primary: true
                        onClicked: { if (shaderStudio.playing) shaderStudio.pause(); else shaderStudio.play() }
                    }
                    VButton { text: "Stop"; tip: "Back to the start"; onClicked: shaderStudio.stop() }
                    VButton {
                        text: shaderStudio.loopMode === "loop" ? "Loop" : shaderStudio.loopMode === "pingpong" ? "Ping-pong" : "Once"
                        tip: "What happens at the end: loop, play backwards and forwards, or stop"
                        onClicked: shaderStudio.loopMode = shaderStudio.loopMode === "loop" ? "pingpong" : shaderStudio.loopMode === "pingpong" ? "once" : "loop"
                    }
                    Text { text: "Speed"; color: theme.dim; font.pixelSize: theme.smallFontSize }
                    Repeater {
                        model: [0.25, 0.5, 1, 2]
                        delegate: VButton {
                            required property real modelData
                            text: modelData + "×"
                            implicitHeight: 24
                            checked: Math.abs(shaderStudio.playbackSpeed - modelData) < 0.01
                            onClicked: shaderStudio.playbackSpeed = modelData
                        }
                    }
                    Text { text: "Length"; color: theme.dim; font.pixelSize: theme.smallFontSize }
                    VNumberField {
                        value: shaderStudio.duration
                        suffix: " s"
                        decimals: 1
                        onEdited: function(v) { shaderStudio.duration = v }
                    }
                    Text {
                        text: shaderStudio.timelineTime.toFixed(2) + " / " + shaderStudio.duration.toFixed(2) + " s"
                        color: theme.text
                        font.pixelSize: theme.fontSize
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "Animation"; color: theme.dim; font.pixelSize: theme.smallFontSize }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 3
                        Repeater {
                            model: shaderStudio.animationModes()
                            delegate: VButton {
                                required property string modelData
                                text: modelData
                                implicitHeight: 24
                                tip: shaderStudio.animationModeTip(modelData)
                                onClicked: shaderStudio.applyAnimationMode(modelData)
                            }
                        }
                    }
                }

                VSlider {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0.1, shaderStudio.duration)
                    value: shaderStudio.timelineTime
                    onDragStarted: shaderStudio.pause()
                    onMoved: function(v) { shaderStudio.playhead = v }
                }

                // The keyframe tracks.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        visible: trackList.count === 0
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: "No keyframes yet. Move the playhead, then click ◆ beside a number to key it; change the value at another time to key that too. Or choose an animation above."
                        color: theme.faint
                        font.pixelSize: theme.smallFontSize
                    }

                    ListView {
                        id: trackList
                        anchors.fill: parent
                        clip: true
                        spacing: 2
                        model: { shaderStudio.animationRevision; return shaderStudio.tracks() }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            id: trackRow
                            required property var modelData
                            width: trackList.width - 10
                            height: 22

                            Text {
                                id: trackLabel
                                width: 150
                                anchors.verticalCenter: parent.verticalCenter
                                text: trackRow.modelData.label
                                color: theme.text
                                font.pixelSize: theme.smallFontSize
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                id: lane
                                anchors.left: trackLabel.right
                                anchors.leftMargin: 8
                                anchors.right: removeButton.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                height: 18
                                radius: 3
                                color: theme.field

                                Rectangle {
                                    x: lane.width * Math.min(1, shaderStudio.timelineTime / Math.max(0.1, shaderStudio.duration))
                                    width: 1
                                    height: parent.height
                                    color: theme.accent
                                }
                                Repeater {
                                    model: trackRow.modelData.keys
                                    delegate: Rectangle {
                                        id: diamond
                                        required property var modelData
                                        required property int index
                                        x: lane.width * Math.min(1, modelData.time / Math.max(0.1, shaderStudio.duration)) - 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 12
                                        height: 12
                                        rotation: 45
                                        radius: 2
                                        color: diamondArea.containsMouse ? "#ffffff" : theme.accent
                                        border.color: "#000000"
                                        MouseArea {
                                            id: diamondArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.RightButton) {
                                                    shaderStudio.removeKey(trackRow.modelData.index, diamond.index)
                                                } else {
                                                    var order = ["linear", "smooth", "easeIn", "easeOut"]
                                                    var next = order[(order.indexOf(diamond.modelData.easing) + 1) % order.length]
                                                    shaderStudio.setKeyEasing(trackRow.modelData.index, diamond.index, next)
                                                }
                                            }
                                            ToolTip.visible: containsMouse
                                            ToolTip.delay: 300
                                            ToolTip.text: "At " + diamond.modelData.time.toFixed(2) + " s: " + diamond.modelData.value.toFixed(2)
                                                          + ". Easing: " + diamond.modelData.easing + " (click to change). Right-click removes."
                                        }
                                    }
                                }
                            }
                            VButton {
                                id: removeButton
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: "×"
                                quiet: true
                                implicitHeight: 20
                                tip: "Remove this animation"
                                onClicked: shaderStudio.removeTrack(trackRow.modelData.index)
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------ status
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: theme.window
            Text {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: shaderStudio.status.length > 0 ? shaderStudio.status
                      : (shaderStudio.hasFile ? shaderStudio.fileName : "Unsaved shader") + (shaderStudio.dirty ? " — changed" : "")
                color: shaderStudio.statusIsError ? theme.error : theme.dim
                font.pixelSize: theme.smallFontSize
            }
        }
    }

    // ---------------------------------------------------------------- reports
    Rectangle {
        anchors.fill: parent
        visible: shaderStudio.report.length > 0
        color: "#b0000000"

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function(wheel) { wheel.accepted = true }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(640, parent.width - 60)
            height: 36 + 24 + 10 + reportFlick.height + 10 + 32
            radius: 8
            color: theme.panel
            border.color: theme.line

            Column {
                id: reportColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10
                Text {
                    width: parent.width
                    text: shaderStudio.reportTitle
                    color: theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Flickable {
                    id: reportFlick
                    width: parent.width
                    height: Math.min(Math.max(80, root.height - 260), reportText.implicitHeight)
                    contentHeight: reportText.implicitHeight
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    Text {
                        id: reportText
                        width: parent.width - 12
                        text: "• " + shaderStudio.report.join("\n\n• ")
                        color: theme.dim
                        font.pixelSize: theme.fontSize
                        wrapMode: Text.WordWrap
                    }
                }
                VButton { text: "Close"; primary: true; onClicked: shaderStudio.clearReport() }
            }
        }
    }
}
