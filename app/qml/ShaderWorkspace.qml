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
    property string leftTab: "library"
    property string lastFamily: ""
    readonly property var backgrounds: ["Dark", "Grey", "Light", "Checker"]

    // Opening a layer shader shows its layers; any other shader shows the library.
    Connections {
        target: shaderStudio
        function onStructureChanged() {
            if (shaderStudio.familyId !== root.lastFamily) {
                root.leftTab = shaderStudio.isLayers ? "layers" : "library"
                root.lastFamily = shaderStudio.familyId
            }
        }
    }

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
        property int echo: TextInput.Normal
        onTextChanged: { if (field.text !== text) field.text = text }
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
            echoMode: box.echo
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

                // Library or layer stack. The stack belongs to the Layer Shader family only.
                RowLayout {
                    id: leftTabs
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 4
                    VButton {
                        Layout.fillWidth: true
                        text: "Library"
                        checked: root.leftTab === "library"
                        tip: "Shader families and presets"
                        onClicked: root.leftTab = "library"
                    }
                    VButton {
                        Layout.fillWidth: true
                        text: shaderStudio.isLayers ? "Layers" : "Build in layers"
                        checked: root.leftTab === "layers"
                        tip: shaderStudio.isLayers ? "The layers this shader is built from" : "Start a shader from layers: shapes, gradients and patterns you stack and animate"
                        onClicked: {
                            if (!shaderStudio.isLayers) shaderStudio.newLayerShader()
                            root.leftTab = "layers"
                        }
                    }
                    VButton {
                        Layout.preferredWidth: 52
                        text: "AI"
                        checked: root.leftTab === "ai"
                        tip: "Describe a shader in words and let an AI model build or change layers"
                        onClicked: root.leftTab = "ai"
                    }
                }

                ColumnLayout {
                    visible: root.leftTab === "library"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: leftTabs.bottom
                    anchors.bottom: parent.bottom
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

                // ---- the layer stack
                ColumnLayout {
                    visible: root.leftTab === "layers"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: leftTabs.bottom
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Heading { text: "LAYERS" }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: layerList.count + " of 8"
                            color: theme.faint
                            font.pixelSize: theme.smallFontSize
                        }
                    }
                    Text {
                        visible: layerList.count === 0
                        Layout.fillWidth: true
                        text: "No layers yet. Add an element below."
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                        wrapMode: Text.WordWrap
                    }

                    ListView {
                        id: layerList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 232)
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: { shaderStudio.revision; shaderStudio.structureRevision; return shaderStudio.layers() }
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            readonly property bool picked: shaderStudio.selectedLayer === modelData.slot
                            property bool editing: false
                            width: layerList.width - 8
                            height: 42
                            radius: 5
                            color: picked ? theme.hover : rowArea.containsMouse ? theme.raised : "transparent"
                            border.width: picked ? 1 : 0
                            border.color: theme.accent

                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: shaderStudio.selectedLayer = row.modelData.slot
                                onDoubleClicked: { shaderStudio.selectedLayer = row.modelData.slot; row.editing = true; nameEdit.forceActiveFocus(); nameEdit.selectAll() }
                            }

                            Text {
                                id: eye
                                x: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.visible ? "●" : "○"
                                color: row.modelData.visible ? theme.accent : theme.faint
                                font.pixelSize: 15
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    onClicked: shaderStudio.toggleLayer(row.modelData.slot)
                                }
                            }
                            Rectangle {
                                id: swatch
                                anchors.left: eye.right
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 22
                                height: 22
                                radius: 4
                                border.width: 1
                                border.color: theme.line
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: row.modelData.colourA }
                                    GradientStop { position: 1; color: row.modelData.colourB }
                                }
                                opacity: row.modelData.visible ? 1 : 0.4
                            }
                            Column {
                                anchors.left: swatch.right
                                anchors.leftMargin: 8
                                anchors.right: tools.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1
                                Text {
                                    visible: !row.editing
                                    width: parent.width
                                    text: row.modelData.name
                                    color: row.modelData.visible ? theme.text : theme.faint
                                    font.pixelSize: theme.fontSize
                                    elide: Text.ElideRight
                                }
                                TextInput {
                                    id: nameEdit
                                    visible: row.editing
                                    width: parent.width
                                    text: row.modelData.name
                                    color: theme.text
                                    selectionColor: theme.accent
                                    selectedTextColor: theme.accentText
                                    font.pixelSize: theme.fontSize
                                    selectByMouse: true
                                    clip: true
                                    onAccepted: { shaderStudio.renameLayer(row.modelData.slot, text); row.editing = false }
                                    onActiveFocusChanged: if (!activeFocus && row.editing) { shaderStudio.renameLayer(row.modelData.slot, text); row.editing = false }
                                    Keys.onEscapePressed: row.editing = false
                                }
                                Text {
                                    width: parent.width
                                    text: row.modelData.group + " · " + row.modelData.blend
                                    color: theme.dim
                                    font.pixelSize: theme.smallFontSize
                                    elide: Text.ElideRight
                                }
                            }
                            Row {
                                id: tools
                                anchors.right: parent.right
                                anchors.rightMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 0
                                VButton { text: "▲"; quiet: true; implicitWidth: 24; implicitHeight: 24; tip: "Move up (drawn later, on top)"; enabled: row.modelData.slot < layerList.count - 1; onClicked: shaderStudio.moveLayer(row.modelData.slot, 1) }
                                VButton { text: "▼"; quiet: true; implicitWidth: 24; implicitHeight: 24; tip: "Move down"; enabled: row.modelData.slot > 0; onClicked: shaderStudio.moveLayer(row.modelData.slot, -1) }
                                VButton { text: "⧉"; quiet: true; implicitWidth: 24; implicitHeight: 24; tip: "Duplicate this layer"; onClicked: shaderStudio.duplicateLayer(row.modelData.slot) }
                                VButton { text: "×"; quiet: true; implicitWidth: 24; implicitHeight: 24; tip: "Delete this layer"; onClicked: shaderStudio.removeLayer(row.modelData.slot) }
                            }
                        }
                    }

                    VButton {
                        Layout.fillWidth: true
                        text: "Whole shader: glow, brightness, opacity"
                        checked: shaderStudio.selectedLayer === -1
                        tip: "Settings that apply to every layer together"
                        onClicked: shaderStudio.selectedLayer = -1
                    }

                    Heading { text: "ADD AN ELEMENT" }
                    ListView {
                        id: elementList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: shaderStudio.elements()
                        ScrollBar.vertical: ScrollBar {}
                        section.property: "group"
                        section.delegate: Text {
                            required property string section
                            width: elementList.width
                            height: 22
                            verticalAlignment: Text.AlignBottom
                            text: section === "Shape" ? "SHAPES" : section === "Gradient" ? "GRADIENTS" : "PATTERNS"
                            color: theme.faint
                            font.pixelSize: theme.smallFontSize
                            font.letterSpacing: 1.1
                        }
                        delegate: Rectangle {
                            id: item
                            required property var modelData
                            width: elementList.width - 8
                            height: 30
                            radius: 4
                            color: itemArea.containsMouse ? theme.raised : "transparent"
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.right: plus.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: item.modelData.name
                                color: theme.text
                                font.pixelSize: theme.fontSize
                                elide: Text.ElideRight
                            }
                            Text {
                                id: plus
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: "+"
                                color: itemArea.containsMouse ? theme.accent : theme.faint
                                font.pixelSize: 16
                            }
                            MouseArea {
                                id: itemArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: shaderStudio.addLayer(item.modelData.index)
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 500
                                ToolTip.text: item.modelData.tip
                            }
                        }
                    }

                    Heading { text: "SAVE TO LIBRARY" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        SearchBox {
                            id: layerPresetName
                            Layout.fillWidth: true
                            placeholder: "Name for this shader"
                        }
                        VButton {
                            text: "Save"
                            tip: "Keep this shader in the library under My presets (Library tab) to open or reuse later"
                            enabled: layerPresetName.text.trim().length > 0
                            onClicked: { shaderStudio.saveUserPreset(layerPresetName.text); layerPresetName.text = "" }
                        }
                    }
                }

                // ---- the AI Shader Artist
                Flickable {
                    id: aiFlick
                    visible: root.leftTab === "ai"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: leftTabs.bottom
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    contentWidth: width
                    contentHeight: aiColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    property bool setupOpen: !shaderAssistant.configured
                    property int providerPick: shaderAssistant.provider

                    ColumnLayout {
                        id: aiColumn
                        width: aiFlick.width
                        spacing: 8

                        Heading { text: "AI SHADER ARTIST" }
                        Text {
                            Layout.fillWidth: true
                            text: shaderAssistant.configured
                                  ? "Connected: " + shaderAssistant.model
                                  : "Not connected. Nothing is sent until you add a key below."
                            color: shaderAssistant.configured ? theme.dim : theme.warn
                            font.pixelSize: theme.smallFontSize
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            color: theme.field
                            border.color: theme.line
                            radius: 4
                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 2
                                TextArea {
                                    id: aiPrompt
                                    wrapMode: TextArea.Wrap
                                    color: theme.text
                                    font.pixelSize: theme.fontSize
                                    placeholderText: "Describe the shader or the change, e.g. a golden energy ring with two counter-rotating bands"
                                    placeholderTextColor: theme.faint
                                    background: null
                                    selectByMouse: true
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            VButton {
                                text: "Generate Shader"
                                primary: true
                                enabled: !shaderAssistant.busy && aiPrompt.text.trim().length > 0
                                tip: "Build a new layer shader from the description. The open shader is replaced only if you apply the result, and Undo brings it back"
                                onClicked: shaderAssistant.send(aiPrompt.text, "generate")
                            }
                            VButton {
                                text: "Refine Selected"
                                enabled: !shaderAssistant.busy && shaderStudio.isLayers && aiPrompt.text.trim().length > 0
                                tip: shaderStudio.selectedLayer >= 0 ? "Change only the selected layer, unless you ask for more" : "Change the open layer shader. Select a layer to limit the change to it"
                                onClicked: shaderAssistant.send(aiPrompt.text, "refine")
                            }
                            VButton {
                                text: "Animate"
                                enabled: !shaderAssistant.busy && shaderStudio.isLayers && aiPrompt.text.trim().length > 0
                                tip: "Add or change keyframes on the timeline only"
                                onClicked: shaderAssistant.send(aiPrompt.text, "animate")
                            }
                            VButton {
                                text: "Explain"
                                enabled: !shaderAssistant.busy && shaderStudio.isLayers
                                tip: "Ask a question about this shader. Nothing is changed"
                                onClicked: shaderAssistant.send(aiPrompt.text.trim().length > 0 ? aiPrompt.text : "Explain how this shader is built and what each layer does.", "explain")
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Not built yet: variations, reference image or video, optimize, diagnose and fix."
                            color: theme.faint
                            font.pixelSize: theme.smallFontSize
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: shaderAssistant.busy
                            Text {
                                Layout.fillWidth: true
                                text: shaderAssistant.message
                                color: theme.dim
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            VButton { text: "Cancel"; onClicked: shaderAssistant.cancel() }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !shaderAssistant.busy && shaderAssistant.message.length > 0
                            text: shaderAssistant.message
                            color: shaderAssistant.messageIsError ? theme.warn : theme.dim
                            font.pixelSize: theme.smallFontSize
                            wrapMode: Text.WordWrap
                        }

                        // answer to Explain
                        Text {
                            Layout.fillWidth: true
                            visible: shaderAssistant.answer.length > 0
                            text: shaderAssistant.answer
                            color: theme.text
                            font.pixelSize: theme.smallFontSize
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                        }

                        // the proposal
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: shaderAssistant.hasProposal
                            spacing: 6
                            Heading { text: "PROPOSED CHANGES" }
                            Text {
                                Layout.fillWidth: true
                                text: shaderAssistant.proposalSummary
                                color: theme.text
                                font.pixelSize: theme.fontSize
                                font.bold: true
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: shaderAssistant.proposalExplanation.length > 0
                                text: shaderAssistant.proposalExplanation
                                color: theme.dim
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: shaderAssistant.proposalChanges
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: "• " + modelData
                                    color: theme.text
                                    font.pixelSize: theme.smallFontSize
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Text {
                                visible: shaderAssistant.proposalRefused.length > 0
                                text: "Not applied:"
                                color: theme.warn
                                font.pixelSize: theme.smallFontSize
                            }
                            Repeater {
                                model: shaderAssistant.proposalRefused
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: "• " + modelData
                                    color: theme.warn
                                    font.pixelSize: theme.smallFontSize
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "The preview shows the proposal. Your shader is unchanged until you apply it."
                                color: theme.faint
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                VButton { text: "Apply"; primary: true; tip: "Keep these changes. Undo takes them back in one step"; onClicked: shaderAssistant.apply() }
                                VButton { text: "Reject"; tip: "Drop the proposal"; onClicked: shaderAssistant.reject() }
                                VButton { text: "Regenerate"; enabled: !shaderAssistant.busy; tip: "Ask again with the same request"; onClicked: shaderAssistant.regenerate() }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            VButton { text: "Undo"; enabled: shaderStudio.canUndo; onClicked: shaderStudio.undo() }
                            VButton { text: "Redo"; enabled: shaderStudio.canRedo; onClicked: shaderStudio.redo() }
                            VButton { text: "New conversation"; tip: "Forget the earlier requests in this chat"; onClicked: shaderAssistant.newConversation() }
                        }

                        // connection
                        VButton {
                            text: aiFlick.setupOpen ? "Hide connection" : "Connect…"
                            onClicked: aiFlick.setupOpen = !aiFlick.setupOpen
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: aiFlick.setupOpen
                            spacing: 6
                            Heading { text: "CONNECTION" }
                            RowLayout {
                                spacing: 6
                                VButton { text: "Claude"; checked: aiFlick.providerPick === 0; onClicked: aiFlick.providerPick = 0 }
                                VButton { text: "OpenAI-compatible"; checked: aiFlick.providerPick === 1; onClicked: aiFlick.providerPick = 1 }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: aiFlick.providerPick === 0
                                      ? "Claude (Anthropic). Your key is sent only to api.anthropic.com."
                                      : "OpenAI or any compatible server. Your key is sent only to the address you give."
                                color: theme.faint
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            SearchBox { id: aiModel; Layout.fillWidth: true; placeholder: aiFlick.providerPick === 0 ? "Model (default claude-sonnet-5-5)" : "Model (default gpt-4o)" }
                            SearchBox { id: aiBase; Layout.fillWidth: true; visible: aiFlick.providerPick === 1; placeholder: "Server address (default https://api.openai.com/v1)" }
                            SearchBox { id: aiKey; Layout.fillWidth: true; echo: TextInput.Password; placeholder: shaderAssistant.hasKey ? "A key is saved. Type a new one to replace it" : "API key" }
                            Text {
                                Layout.fillWidth: true
                                text: "The key is stored on this computer only, in the app's settings, not encrypted. It is never put in a shader file or an export."
                                color: theme.faint
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                VButton {
                                    text: "Save connection"
                                    primary: true
                                    onClicked: { shaderAssistant.configure(aiFlick.providerPick, aiModel.text, aiBase.text, aiKey.text); aiKey.text = "" }
                                }
                                VButton { text: "Remove saved key"; enabled: shaderAssistant.hasKey; onClicked: shaderAssistant.forgetKey() }
                            }
                        }
                        Item { Layout.preferredHeight: 12 }
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
                    VButton {
                        visible: shaderStudio.hasModel
                        text: "Turn model"
                        implicitHeight: 24
                        tip: "Upside down or lying on its side? Each press tries the next way up: flipped, Z up, Z down, as it came"
                        onClicked: shaderStudio.turnPreviewModel()
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
