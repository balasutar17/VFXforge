import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import VfxForge 1.0

ApplicationWindow {
    id: root

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: app.windowTitle
    color: theme.window

    // ------------------------------------------------------------ actions

    // What to do once the question "save your changes first?" is settled.
    property var pendingAction: null
    property var afterSave: null
    property bool closeConfirmed: false
    // True while a question is on screen; keyboard shortcuts wait for it.
    readonly property bool asking: unsaved.visible

    Component.onCompleted: if (showLibraryAtStart) app.libraryOpen = true

    // Opening a preset or a file says which part of the world to look at.
    Connections {
        target: app
        function onViewSuggested(x, y, unitsHigh) {
            if (unitsHigh > 0)
                viewport.frame(x, y, unitsHigh)
            else
                viewport.resetView()
        }
    }

    function guard(action) {
        if (app.dirty) {
            pendingAction = action
            unsaved.visible = true
        } else {
            action()
        }
    }

    function saveThen(then) {
        if (app.hasFile && !app.readOnly) {
            if (app.save() && then)
                then()
        } else {
            afterSave = then
            saveDialog.open()
        }
    }

    function saveAsThen(then) {
        afterSave = then
        saveDialog.open()
    }

    onClosing: function(close) {
        if (app.dirty && !closeConfirmed) {
            close.accepted = false
            guard(function() {
                root.closeConfirmed = true
                root.close()
            })
        }
    }

    // The heartbeat: once per screen refresh, move time on and redraw. A
    // hiccup is capped so the effect never leaps ahead after a stall.
    FrameAnimation {
        running: true
        onTriggered: {
            app.tick(Math.min(frameTime, 0.1))
            reference.tick(Math.min(frameTime, 0.1))
        }
    }

    Shortcut { enabled: !root.asking; sequences: [StandardKey.New]; onActivated: root.guard(function() { app.newEffect(false) }) }
    Shortcut { enabled: !root.asking; sequences: [StandardKey.Open]; onActivated: root.guard(function() { openDialog.open() }) }
    Shortcut { enabled: !root.asking; sequences: [StandardKey.Save]; onActivated: root.saveThen(null) }
    Shortcut { enabled: !root.asking; sequences: [StandardKey.SaveAs]; onActivated: root.saveAsThen(null) }
    Shortcut { enabled: !root.asking; sequences: [StandardKey.Undo]; onActivated: app.undo() }
    Shortcut { enabled: !root.asking; sequences: [StandardKey.Redo]; onActivated: app.redo() }
    Shortcut { enabled: !root.asking; sequence: "Space"; onActivated: app.togglePlay() }
    Shortcut { enabled: !root.asking; sequence: "Home"; onActivated: app.restart() }
    Shortcut { enabled: !root.asking; sequence: ","; onActivated: app.stepFrames(-1) }
    Shortcut { enabled: !root.asking; sequence: "."; onActivated: app.stepFrames(1) }
    Shortcut { enabled: !root.asking && app.libraryOpen; sequence: "Escape"; onActivated: app.libraryOpen = false }
    Shortcut { enabled: !root.asking && app.exportOpen; sequence: "Escape"; onActivated: app.exportOpen = false }
    Shortcut { enabled: !root.asking; sequence: "Ctrl+E"; onActivated: app.exportOpen = true }
    Shortcut { enabled: !root.asking && reference.open; sequence: "Escape"; onActivated: reference.open = false }
    Shortcut { enabled: !root.asking; sequence: "Ctrl+R"; onActivated: reference.open = true }
    // The handles: W move, E turn, R size (as in most 3D programs).
    Shortcut { enabled: !root.asking && !app.libraryOpen; sequence: "W"; onActivated: app.gizmoMode = "move" }
    Shortcut { enabled: !root.asking && !app.libraryOpen; sequence: "E"; onActivated: app.gizmoMode = "turn" }
    Shortcut { enabled: !root.asking && !app.libraryOpen; sequence: "R"; onActivated: app.gizmoMode = "size" }
    Shortcut { enabled: !root.asking; sequence: "Ctrl+G"; onActivated: app.groupSelection() }
    Shortcut { enabled: !root.asking; sequence: "Ctrl+Shift+G"; onActivated: app.ungroupSelection() }
    Shortcut { enabled: !root.asking; sequence: "Ctrl+D"; onActivated: app.duplicateSelection() }

    // The menu of the layer list: right-click a layer or a group heading. It
    // acts on what is selected (a row outside the selection becomes the
    // selection first), so it copies and groups several layers at once.
    function openLayerMenu(item, mouse) {
        var p = item.mapToItem(Overlay.overlay, mouse.x, mouse.y)
        layerMenu.x = Math.max(4, Math.min(p.x, root.width - layerMenu.width - 4))
        layerMenu.y = Math.max(4, Math.min(p.y, root.height - 190))
        layerMenu.open()
    }
    Popup {
        id: layerMenu
        objectName: "layerMenu"
        parent: Overlay.overlay
        width: 230
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: theme.panel; border.color: theme.line; radius: 8 }
        readonly property bool onGroup: app.selectedGroup !== ""
        readonly property int count: app.selectionCount

        contentItem: Column {
            spacing: 2
            VButton {
                objectName: "menuDuplicate"
                width: parent.width
                quiet: true
                text: layerMenu.onGroup ? "Duplicate group"
                    : layerMenu.count > 1 ? "Duplicate " + layerMenu.count + " layers" : "Duplicate layer"
                tip: "Make a copy of what is selected (Cmd+D)"
                onClicked: { layerMenu.close(); app.duplicateSelection() }
            }
            VButton {
                objectName: "menuGroup"
                width: parent.width
                visible: layerMenu.count > 1 && !layerMenu.onGroup
                height: visible ? implicitHeight : 0
                quiet: true
                text: "Group " + layerMenu.count + " layers"
                tip: "Make the selected layers a group (Cmd+G)"
                onClicked: { layerMenu.close(); app.groupSelection() }
            }
            VButton {
                objectName: "menuUngroup"
                width: parent.width
                visible: layerMenu.onGroup
                height: visible ? implicitHeight : 0
                quiet: true
                text: "Ungroup"
                tip: "Keep the layers, one group level up (Cmd+Shift+G)"
                onClicked: { layerMenu.close(); app.ungroupSelection() }
            }
            VButton {
                objectName: "menuRemove"
                width: parent.width
                visible: layerMenu.onGroup || layerMenu.count === 1
                height: visible ? implicitHeight : 0
                quiet: true
                text: layerMenu.onGroup ? "Remove group" : "Remove layer"
                tip: "Undo brings it back"
                onClicked: {
                    layerMenu.close()
                    if (layerMenu.onGroup) app.removeGroup(app.selectedGroup)
                    else app.removeLayer(app.selectedLayer)
                }
            }
        }
    }

    FileDialog {
        id: backdropDialog
        title: "Choose a picture to show behind the effect"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.bmp *.gif)", "All files (*)"]
        onAccepted: app.setBackdrop(selectedFile)
    }

    FileDialog {
        id: targetDialog
        title: "Choose the symbol the effect is for"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: app.setTargetFile(selectedFile)
    }

    FileDialog {
        id: modelDialog
        title: "Choose a 3D model to make the effect on"
        fileMode: FileDialog.OpenFile
        nameFilters: ["3D models (*.obj *.glb *.gltf)", "All files (*)"]
        onAccepted: app.useModel(selectedFile)
    }

    FileDialog {
        id: openDialog
        title: "Open an effect"
        fileMode: FileDialog.OpenFile
        nameFilters: ["VFX Forge effects (*.vfx)", "All files (*)"]
        onAccepted: app.openFile(selectedFile)
    }

    FileDialog {
        id: saveDialog
        title: "Save the effect"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "vfx"
        nameFilters: ["VFX Forge effects (*.vfx)"]
        onAccepted: {
            var then = root.afterSave
            root.afterSave = null
            if (app.saveAs(selectedFile) && then)
                then()
        }
        onRejected: root.afterSave = null
    }

    // ------------------------------------------------------------- layout

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- top bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 12
                spacing: 6

                VButton { primary: true; text: "Library"; tip: "Browse the ready-made effects"; onClicked: app.libraryOpen = true }
                VButton { text: "From a reference…"; tip: "Rebuild an effect from a picture, a GIF or a short video of it (Ctrl/Cmd+R)"; onClicked: reference.open = true }
                VButton { text: "New 2D"; tip: "Start a new flat effect"; onClicked: root.guard(function() { app.newEffect(false) }) }
                VButton { text: "New 3D"; tip: "Start a new effect with depth"; onClicked: root.guard(function() { app.newEffect(true) }) }
                VButton { text: "Open…"; tip: "Open a .vfx file"; onClicked: root.guard(function() { openDialog.open() }) }
                VButton { text: "Save"; tip: "Save this effect"; enabled: app.dirty || !app.hasFile; onClicked: root.saveThen(null) }
                VButton { text: "Save As…"; tip: "Save a copy under a new name"; onClicked: root.saveAsThen(null) }
                VButton {
                    objectName: "addToLibrary"
                    text: "Add to Library"
                    tip: "Keep this effect in your own library, under the effect's name, to open or add to other effects later. The same name replaces the one there."
                    onClicked: app.saveToLibrary(viewport.viewState())
                }
                VButton { text: "Export…"; tip: "To Unity, or as animation frames (Ctrl/Cmd+E)"; onClicked: app.exportOpen = true }
                // One-click changes to the whole effect, built on the
                // animation principles. Locked layers are left alone.
                VButton {
                    id: motionButton
                    text: "Motion…"
                    tip: "Cartoon motion for the whole effect: on twos, squash and stretch, anticipation, overshoot, curl"
                    onClicked: motionMenu.open()

                    Popup {
                        id: motionMenu
                        objectName: "motionMenu"
                        y: motionButton.height + 8
                        width: 372
                        padding: 12
                        modal: true
                        background: Rectangle { color: theme.panel; border.color: theme.line; radius: 8 }

                        contentItem: Column {
                            spacing: 8
                            Text {
                                width: parent.width
                                text: "Changes every layer that is not locked. Each is one step to undo."
                                color: theme.dim
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: app.motionChanges
                                delegate: Item {
                                    id: motionRow
                                    required property var modelData
                                    width: 348
                                    height: Math.max(26, motionHelp.implicitHeight)
                                    VButton {
                                        id: motionApply
                                        width: 138
                                        height: 26
                                        text: motionRow.modelData.label
                                        onClicked: app.applyMotionChange(motionRow.modelData.key)
                                    }
                                    Text {
                                        id: motionHelp
                                        anchors.left: motionApply.right
                                        anchors.leftMargin: 10
                                        anchors.right: parent.right
                                        text: motionRow.modelData.help
                                        color: theme.dim
                                        font.pixelSize: theme.smallFontSize
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; Layout.leftMargin: 6; Layout.rightMargin: 6; color: theme.line }

                VButton { text: "Undo"; enabled: app.canUndo; tip: app.canUndo ? "Undo: " + app.undoName : "Nothing to undo"; onClicked: app.undo() }
                VButton { text: "Redo"; enabled: app.canRedo; tip: app.canRedo ? "Redo: " + app.redoName : "Nothing to redo"; onClicked: app.redo() }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Expert view (node graph): NOT IMPLEMENTED yet"
                    color: theme.faint
                    font.pixelSize: theme.smallFontSize
                }
                Rectangle {
                    Layout.preferredWidth: modeLabel.implicitWidth + 20
                    Layout.preferredHeight: 24
                    radius: 12
                    color: "transparent"
                    border.color: theme.accent
                    Text {
                        id: modeLabel
                        anchors.centerIn: parent
                        text: "Simple"
                        color: theme.accent
                        font.pixelSize: theme.smallFontSize
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.line }

        // ---- three columns
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- left: the effect and its layers
            Rectangle {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                color: theme.panel

                // The whole column scrolls when the effect, target, model and
                // layer sections together are taller than the window.
                Flickable {
                    id: leftScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: leftColumn.implicitHeight + 24
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: leftColumn
                        x: 12
                        y: 12
                        width: leftScroll.width - 24
                        spacing: 6

                        PanelTitle { Layout.fillWidth: true; text: "Effect"; detail: app.threeD ? "3D" : "2D" }

                        VTextField {
                            Layout.fillWidth: true
                            text: app.effectName
                            onEdited: function(newText) { app.setEffectName(newText) }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6

                            Text { text: "Length (seconds)"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.fillWidth: true }
                            VNumberField { value: app.duration; decimals: 2; onEdited: function(v) { app.setDuration(v) } }

                            Text { text: "Frames per second"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.fillWidth: true }
                            VNumberField { value: app.frameRate; decimals: 0; onEdited: function(v) { app.setFrameRate(v) } }

                            Text { text: "Variation number"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.fillWidth: true }
                            VNumberField { value: app.seed; decimals: 0; onEdited: function(v) { app.setSeed(v) } }
                        }

                        VButton {
                            Layout.fillWidth: true
                            text: "Try another variation"
                            tip: "Same settings, different random result"
                            onClicked: app.newVariation()
                        }

                        Item { Layout.preferredHeight: 6 }
                        PanelTitle { Layout.fillWidth: true; text: "Target"; detail: app.targetName }
                        VButton {
                            Layout.fillWidth: true
                            visible: !app.hasTarget
                            text: "Import a symbol…"
                            tip: "The picture the effect is for: a slot symbol, a logo, a gem. A PNG with a see-through "
                                 + "background works best. Effects from Symbol Outlines follow its shape."
                            onClicked: targetDialog.open()
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            visible: app.hasTarget
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            Text { text: "Width (units)"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.fillWidth: true }
                            VNumberField { value: app.targetWidth; decimals: 2; onEdited: function(v) { app.placeTarget(v, app.targetX, app.targetY) } }
                            Text { text: "X"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VNumberField { value: app.targetX; decimals: 2; onEdited: function(v) { app.placeTarget(app.targetWidth, v, app.targetY) } }
                            Text { text: "Y"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VNumberField { value: app.targetY; decimals: 2; onEdited: function(v) { app.placeTarget(app.targetWidth, app.targetX, v) } }
                        }
                        VButton {
                            Layout.fillWidth: true
                            visible: app.hasTarget
                            text: "Fit selected layers to it"
                            tip: "The selected layers send their particles from the target's outline (Shape: Path, Fit to target). "
                                 + "Padding, Thickness and Travel under Shape shape the outline effect."
                            onClicked: app.fitSelectionToTarget()
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: app.hasTarget
                            spacing: 6
                            VButton { Layout.fillWidth: true; text: "Change…"; tip: "Another symbol: the fitted layers follow its shape"; onClicked: targetDialog.open() }
                            VButton { Layout.fillWidth: true; text: "Remove"; onClicked: app.clearTarget() }
                        }

                        Item { Layout.preferredHeight: 6 }
                        PanelTitle { Layout.fillWidth: true; text: "Backdrop"; detail: app.backdropName }

                        VButton {
                            Layout.fillWidth: true
                            visible: !app.hasBackdrop
                            text: "Import a picture…"
                            tip: "Show a character, button or scene behind the effect"
                            onClicked: backdropDialog.open()
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            visible: app.hasBackdrop
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6

                            Text { text: "Height (units)"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.fillWidth: true }
                            VNumberField { value: app.backdropHeight; decimals: 2; onEdited: function(v) { app.setBackdropPlace(app.backdropX, app.backdropY, v) } }

                            Text { text: "Opacity"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: app.backdropOpacity
                                onMoved: function(v) { app.setBackdropOpacity(v) }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: app.hasBackdrop
                            spacing: 6
                            VButton { Layout.fillWidth: true; text: "Change…"; onClicked: backdropDialog.open() }
                            VButton { Layout.fillWidth: true; text: "Remove"; onClicked: app.clearBackdrop() }
                        }

                        // A 3D model to make the effect on, drawn under the particles.
                        Item { Layout.preferredHeight: 6 }
                        PanelTitle {
                            Layout.fillWidth: true
                            text: "Model"
                            detail: app.hasModel ? app.modelName : ""
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: !app.hasModel
                            spacing: 6
                            VButton {
                                Layout.fillWidth: true
                                text: "Import a model…"
                                tip: "Show a character, weapon or prop (.obj, .glb, .gltf) and make the effect on it"
                                onClicked: modelDialog.open()
                            }
                            VButton {
                                objectName: "hardSample"
                                text: "Block"
                                quiet: true
                                tip: "Try a hard-surface test piece (a box, a tower and a pipe) for outlines and scans"
                                onClicked: app.useHardSurfaceModel()
                            }
                            VButton {
                                objectName: "practiceDummy"
                                text: "Dummy"
                                quiet: true
                                tip: "Try a practice dummy, made for trying effects on"
                                onClicked: app.useSampleModel()
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            visible: app.hasModel
                            columns: 4
                            columnSpacing: 6
                            rowSpacing: 6

                            Text { text: "Height"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VNumberField {
                                Layout.fillWidth: true
                                value: app.modelPlace.height
                                decimals: 2
                                onEdited: function(v) { app.setModelPlace("height", v) }
                            }
                            Text { text: "Turn"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VNumberField {
                                Layout.fillWidth: true
                                value: app.modelPlace.turn
                                decimals: 0
                                onEdited: function(v) { app.setModelPlace("turn", v) }
                            }
                            Text { text: "X"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VNumberField {
                                Layout.fillWidth: true
                                value: app.modelPlace.x
                                decimals: 2
                                onEdited: function(v) { app.setModelPlace("x", v) }
                            }
                            Text { text: "Y"; color: theme.dim; font.pixelSize: theme.fontSize }
                            VNumberField {
                                Layout.fillWidth: true
                                value: app.modelPlace.y
                                decimals: 2
                                onEdited: function(v) { app.setModelPlace("y", v) }
                            }
                            Text { text: "Z"; color: theme.dim; font.pixelSize: theme.fontSize; visible: app.threeD }
                            VNumberField {
                                Layout.fillWidth: true
                                visible: app.threeD
                                value: app.modelPlace.z
                                decimals: 2
                                onEdited: function(v) { app.setModelPlace("z", v) }
                            }
                        }
                        VCheck {
                            Layout.fillWidth: true
                            visible: app.hasModel
                            text: "Hide particles behind it"
                            checked: app.hideBehindModel
                            onToggled: function(on) { app.hideBehindModel = on }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: app.hasModel
                            spacing: 6
                            VButton { Layout.fillWidth: true; text: "Change…"; onClicked: modelDialog.open() }
                            VButton { Layout.fillWidth: true; text: "Remove"; onClicked: app.clearModel() }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: app.hasModel
                            spacing: 6
                            Text { text: "Look"; color: theme.dim; font.pixelSize: theme.fontSize }
                            Repeater {
                                model: ["Solid", "Hologram"]
                                VButton {
                                    objectName: index === 1 ? "lookHologram" : "lookSolid"
                                    Layout.fillWidth: true
                                    text: modelData
                                    quiet: app.modelPlace.look !== index
                                    tip: index === 0 ? "The model drawn as a solid, toon-shaded object"
                                                     : "A translucent cyan glass with a glowing rim"
                                    onClicked: app.setModelPlace("look", index)
                                }
                            }
                        }

                        // Effects made from the model's own shape: outlines along its
                        // real edges and silhouette, an energy coat and scan bands on its
                        // surfaces, and rings fitted to its footprint.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: app.hasModel
                            spacing: 6
                            Text { text: "Quality"; color: theme.dim; font.pixelSize: theme.fontSize }
                            Repeater {
                                model: ["Preview", "Balanced", "High"]
                                VButton {
                                    Layout.fillWidth: true
                                    text: modelData
                                    quiet: app.meshQuality !== index
                                    tip: index === 0 ? "Fewer samples: quick on big models"
                                                     : index === 1 ? "A good balance" : "Most detail: slower on dense models"
                                    onClicked: app.meshQuality = index
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: app.hasModel
                            spacing: 6
                            VButton {
                                objectName: "generateOnModel"
                                Layout.fillWidth: true
                                text: "Generate on model"
                                tip: "Make layers that follow this model's real surface, edges and footprint (3D effects)"
                                onClicked: app.generateOnModel(viewport.viewState())
                            }
                            VButton {
                                objectName: "fitToModel"
                                Layout.fillWidth: true
                                visible: app.hasMeshVfx
                                text: "Fit to model"
                                quiet: true
                                tip: "Follow the model again after it moved or changed; your settings stay"
                                onClicked: app.fitToModel(viewport.viewState())
                            }
                        }

                        Item { Layout.preferredHeight: 6 }
                        PanelTitle { Layout.fillWidth: true; text: "Layers" }

                        // The layers, under the headings of the groups they are in
                        // (groups can hold groups). Click to select one; Cmd-click
                        // (Ctrl-click) to add or take away; Shift-click for all
                        // between. Click a heading to select the whole group; its
                        // triangle folds it.
                        ListView {
                            id: layerList
                            objectName: "layerList"
                            Layout.fillWidth: true
                            // Tall enough for its rows, up to a limit; after that the list scrolls by itself.
                            Layout.preferredHeight: Math.max(220, Math.min(app.layerRows.length * 34 + 8, 520))
                            clip: true
                            spacing: 2
                            model: app.layerRows
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Item {
                                id: rowItem
                                required property var modelData
                                required property int index
                                readonly property bool isGroup: modelData.kind === "group"
                                readonly property real indent: (modelData.depth || 0) * 12
                                width: layerList.width
                                height: isGroup ? 28 : 32

                                // ---- a group's heading
                                Rectangle {
                                    visible: rowItem.isGroup
                                    anchors.fill: parent
                                    anchors.leftMargin: rowItem.indent
                                    radius: 5
                                    color: rowItem.modelData.selected ? theme.hover : theme.raised
                                    border.width: rowItem.modelData.selected ? 1 : 0
                                    border.color: theme.accent
                                    MouseArea {
                                        id: groupArea
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                app.prepareGroupMenu(rowItem.modelData.path)
                                                root.openLayerMenu(groupArea, mouse)
                                            } else {
                                                app.clickGroup(rowItem.modelData.path, mouse.modifiers)
                                            }
                                        }
                                    }
                                    Text {
                                        id: foldMark
                                        anchors.left: parent.left
                                        anchors.leftMargin: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: rowItem.modelData.folded ? "\u25b8" : "\u25be"
                                        color: theme.dim
                                        font.pixelSize: theme.fontSize
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            onClicked: app.toggleFold(rowItem.modelData.path)
                                        }
                                    }
                                    Text {
                                        anchors.left: foldMark.right
                                        anchors.leftMargin: 4
                                        anchors.right: groupLock.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: rowItem.modelData.name + "  \u00b7  " + rowItem.modelData.count
                                        color: rowItem.modelData.shown ? theme.text : theme.faint
                                        font.pixelSize: theme.fontSize
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    VButton {
                                        id: groupLock
                                        anchors.right: groupShown.left
                                        anchors.rightMargin: 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        implicitWidth: 24
                                        implicitHeight: 22
                                        quiet: true
                                        checked: rowItem.modelData.locked === true
                                        text: checked ? "\uD83D\uDD12" : "\uD83D\uDD13"
                                        tip: checked ? "Locked: the handles leave this group alone. Click to unlock."
                                                     : "Lock the group so the handles leave it alone"
                                        onClicked: app.setGroupLocked(rowItem.modelData.path, !checked)
                                    }
                                    VCheck {
                                        id: groupShown
                                        anchors.right: groupRemove.left
                                        anchors.rightMargin: 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: rowItem.modelData.shown === true
                                        onToggled: function(nowChecked) { app.setGroupEnabled(rowItem.modelData.path, nowChecked) }
                                    }
                                    VButton {
                                        id: groupRemove
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        implicitWidth: 24
                                        implicitHeight: 22
                                        quiet: true
                                        text: "\u00d7"
                                        tip: "Remove the whole group"
                                        onClicked: app.removeGroup(rowItem.modelData.path)
                                    }
                                }

                                // ---- a layer
                                Rectangle {
                                    id: layerRow
                                    visible: !rowItem.isGroup
                                    readonly property bool active: rowItem.modelData.active === true
                                    readonly property bool chosen: rowItem.modelData.selected === true
                                    anchors.fill: parent
                                    anchors.leftMargin: rowItem.indent
                                    radius: 5
                                    color: chosen ? theme.raised : (rowArea.containsMouse ? theme.hover : "transparent")
                                    border.width: chosen ? 1 : 0
                                    border.color: active ? theme.accent : theme.line

                                    MouseArea {
                                        id: rowArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                app.prepareLayerMenu(rowItem.modelData.index)
                                                root.openLayerMenu(rowArea, mouse)
                                            } else {
                                                app.clickLayer(rowItem.modelData.index, mouse.modifiers)
                                            }
                                        }
                                    }
                                    VCheck {
                                        id: enabledBox
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: rowItem.modelData.enabled === true
                                        onToggled: function(nowChecked) { app.setLayerEnabled(rowItem.modelData.index, nowChecked) }
                                    }
                                    Text {
                                        visible: !layerRow.active
                                        anchors.left: enabledBox.right
                                        anchors.leftMargin: 8
                                        anchors.right: lockMark.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: rowItem.modelData.name || ""
                                        color: rowItem.modelData.enabled ? theme.text : theme.faint
                                        font.pixelSize: theme.fontSize
                                        elide: Text.ElideRight
                                    }
                                    VTextField {
                                        visible: layerRow.active
                                        anchors.left: enabledBox.right
                                        anchors.leftMargin: 6
                                        anchors.right: lockMark.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: rowItem.modelData.name || ""
                                        onEdited: function(newText) { app.renameLayer(rowItem.modelData.index, newText) }
                                    }
                                    Text {
                                        id: lockMark
                                        anchors.right: removeButton.left
                                        anchors.rightMargin: 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: rowItem.modelData.locked ? "\uD83D\uDD12" : ""
                                        font.pixelSize: theme.smallFontSize
                                    }
                                    VButton {
                                        id: removeButton
                                        visible: layerRow.active
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        implicitWidth: layerRow.active ? 26 : 0
                                        implicitHeight: 24
                                        quiet: true
                                        text: "\u00d7"
                                        tip: "Remove this layer"
                                        onClicked: app.removeLayer(rowItem.modelData.index)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: app.selectionCount > 1 || app.selectedGroup !== ""
                            VButton {
                                Layout.fillWidth: true
                                visible: app.selectionCount > 1 && app.selectedGroup === ""
                                text: "Group " + app.selectionCount + " layers"
                                tip: "Make the selected layers a group (Cmd+G)"
                                onClicked: app.groupSelection()
                            }
                            VButton {
                                Layout.fillWidth: true
                                visible: app.selectedGroup !== ""
                                text: "Ungroup"
                                tip: "Keep the layers, one group level up (Cmd+Shift+G)"
                                onClicked: app.ungroupSelection()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            enabled: app.selectedLayer >= 0 && app.selectedLayer < app.layers.length
                            VButton { Layout.fillWidth: true; text: "\u2191"; tip: "Move the selected layer up: drawn earlier, further back"; onClicked: app.moveLayer(app.selectedLayer, -1) }
                            VButton { Layout.fillWidth: true; text: "\u2193"; tip: "Move the selected layer down: drawn later, in front"; onClicked: app.moveLayer(app.selectedLayer, 1) }
                            VButton { Layout.fillWidth: true; text: "Duplicate"; tip: "Make a copy of what is selected: a layer, several layers or a group (Cmd+D). Right-click a layer for more."; onClicked: app.duplicateSelection() }
                            VButton {
                                Layout.fillWidth: true
                                readonly property bool locked: parent.enabled && app.layers[app.selectedLayer].locked === true
                                text: locked ? "Locked" : "Lock"
                                checked: locked
                                tip: "A locked layer is left alone by Refine and by rebuilding from a reference"
                                onClicked: app.setLayerLocked(app.selectedLayer, !locked)
                            }
                        }
                        VButton {
                            Layout.fillWidth: true
                            text: "Add a layer"
                            tip: "Add another emitter to this effect"
                            onClicked: app.addLayer()
                        }

                        // The group of the selected layer: moved, delayed and
                        // sized as one. Each click is one undo step.
                        ColumnLayout {
                            id: groupPanel
                            objectName: "groupPanel"
                            readonly property string group: app.selectedGroup
                            readonly property var row: {
                                var rows = app.layerRows
                                for (var i = 0; i < rows.length; ++i)
                                    if (rows[i].kind === "group" && rows[i].path === group)
                                        return rows[i]
                                return null
                            }
                            readonly property real step: 0.25
                            Layout.fillWidth: true
                            visible: group !== ""
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: "GROUP  " + groupPanel.group.split("/").pop()
                                    color: theme.dim
                                    font.pixelSize: theme.smallFontSize
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                VButton {
                                    implicitHeight: 22
                                    quiet: true
                                    text: "Ungroup"
                                    tip: "Keep the layers, but no longer as a group"
                                    onClicked: app.ungroup(groupPanel.group)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                VButton { Layout.fillWidth: true; implicitHeight: 26; text: "\u2190"; tip: "Move the group left"; onClicked: app.moveGroup(groupPanel.group, -groupPanel.step, 0, 0) }
                                VButton { Layout.fillWidth: true; implicitHeight: 26; text: "\u2192"; tip: "Move the group right"; onClicked: app.moveGroup(groupPanel.group, groupPanel.step, 0, 0) }
                                VButton { Layout.fillWidth: true; implicitHeight: 26; text: "\u2191"; tip: "Move the group up"; onClicked: app.moveGroup(groupPanel.group, 0, groupPanel.step, 0) }
                                VButton { Layout.fillWidth: true; implicitHeight: 26; text: "\u2193"; tip: "Move the group down"; onClicked: app.moveGroup(groupPanel.group, 0, -groupPanel.step, 0) }
                                VButton { Layout.fillWidth: true; implicitHeight: 26; visible: app.threeD; text: "\u2299"; tip: "Move the group nearer, towards the front"; onClicked: app.moveGroup(groupPanel.group, 0, 0, groupPanel.step) }
                                VButton { Layout.fillWidth: true; implicitHeight: 26; visible: app.threeD; text: "\u2297"; tip: "Move the group further, towards the back"; onClicked: app.moveGroup(groupPanel.group, 0, 0, -groupPanel.step) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                VButton { implicitWidth: 26; implicitHeight: 26; text: "\u2212"; tip: "Start the group a tenth of a second earlier"; onClicked: app.delayGroup(groupPanel.group, -0.1) }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: "starts " + (groupPanel.row ? groupPanel.row.start : 0).toFixed(1) + " s"
                                    color: theme.text
                                    font.pixelSize: theme.smallFontSize
                                }
                                VButton { implicitWidth: 26; implicitHeight: 26; text: "+"; tip: "Start the group a tenth of a second later"; onClicked: app.delayGroup(groupPanel.group, 0.1) }
                                Item { Layout.preferredWidth: 6 }
                                VButton { implicitWidth: 26; implicitHeight: 26; text: "\u2212"; tip: "The whole group 10% smaller, particles and reach"; onClicked: app.sizeGroup(groupPanel.group, 1.0 / 1.1) }
                                Text { text: "size"; color: theme.text; font.pixelSize: theme.smallFontSize }
                                VButton { implicitWidth: 26; implicitHeight: 26; text: "+"; tip: "The whole group 10% larger, particles and reach"; onClicked: app.sizeGroup(groupPanel.group, 1.1) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                VButton { Layout.fillWidth: true; implicitHeight: 26; text: "\u21ba 15\u00b0"; tip: "Turn the group 15 degrees counter-clockwise"; onClicked: app.turnSelection(15) }
                                VButton { Layout.fillWidth: true; implicitHeight: 26; text: "\u21bb 15\u00b0"; tip: "Turn the group 15 degrees clockwise"; onClicked: app.turnSelection(-15) }
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.line }

            // ---- middle: the viewport and the transport
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: app.previewBackground === "black" ? "#000000"
                         : app.previewBackground === "white" ? "#ffffff" : theme.viewport
                    clip: true

                    // A checkerboard behind everything, to judge see-through parts.
                    Canvas {
                        anchors.fill: parent
                        visible: app.previewBackground === "checker"
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onVisibleChanged: requestPaint()
                        onPaint: {
                            var g = getContext("2d")
                            var cell = 12
                            for (var y = 0; y < height; y += cell)
                                for (var x = 0; x < width; x += cell) {
                                    g.fillStyle = ((x / cell + y / cell) % 2 === 0) ? "#5a5a5a" : "#3c3c3c"
                                    g.fillRect(x, y, cell, cell)
                                }
                        }
                    }

                    // The backdrop: a picture to work against, behind everything.
                    Image {
                        visible: app.hasBackdrop && viewport.unitScale > 0
                        source: app.backdropSource
                        asynchronous: true
                        smooth: true
                        mipmap: true
                        opacity: app.backdropOpacity
                        height: app.backdropHeight * viewport.unitScale
                        width: implicitHeight > 0 ? height * implicitWidth / implicitHeight : height
                        x: viewport.origin.x + app.backdropX * viewport.unitScale - width / 2
                        y: viewport.origin.y - app.backdropY * viewport.unitScale - height / 2
                    }

                    // The target: the symbol the effect is made for, under the effect.
                    Image {
                        id: targetPicture
                        objectName: "targetPicture"
                        property var middle: { app.targetX; app.targetY; viewport.unitScale; viewport.origin; viewport.width; viewport.height; return viewport.project(app.targetX, app.targetY, 0) }
                        visible: app.hasTarget && app.previewMode !== "effect" && middle.ok
                        source: app.targetSource
                        asynchronous: true
                        smooth: true
                        mipmap: true
                        width: app.targetWidth * middle.scale
                        height: app.targetHeight * middle.scale
                        x: middle.x - width / 2
                        y: middle.y - height / 2
                    }

                    // Where the effect starts from.
                    Item {
                        visible: viewport.originVisible
                        x: viewport.origin.x
                        y: viewport.origin.y
                        Rectangle { x: -9; y: 0; width: 19; height: 1; color: theme.faint; opacity: 0.7 }
                        Rectangle { x: 0; y: -9; width: 1; height: 19; color: theme.faint; opacity: 0.7 }
                    }

                    Viewport {
                        id: viewport
                        anchors.fill: parent
                        controller: app
                        visible: app.previewMode !== "target"
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        property real lastX: 0
                        property real lastY: 0
                        property real travelled: 0
                        onPressed: function(mouse) {
                            lastX = mouse.x
                            lastY = mouse.y
                            travelled = 0
                            forceActiveFocus()
                        }
                        onReleased: function(mouse) {
                            // A click (not a drag) on nothing: nothing selected.
                            if (travelled < 4 && mouse.button === Qt.LeftButton && handlesButton.checked)
                                app.clickViewport(-1, mouse.modifiers)
                        }
                        onPositionChanged: function(mouse) {
                            if ((mouse.modifiers & Qt.AltModifier) && app.hasBackdrop && viewport.unitScale > 0) {
                                // Option-drag moves the backdrop, not the camera.
                                app.moveBackdrop((mouse.x - lastX) / viewport.unitScale,
                                                 -(mouse.y - lastY) / viewport.unitScale)
                            } else {
                                var secondary = mouse.buttons !== Qt.LeftButton || (mouse.modifiers & Qt.ShiftModifier)
                                viewport.dragBy(mouse.x - lastX, mouse.y - lastY, secondary ? true : false)
                            }
                            travelled += Math.abs(mouse.x - lastX) + Math.abs(mouse.y - lastY)
                            lastX = mouse.x
                            lastY = mouse.y
                        }
                        onWheel: function(wheel) {
                            // Some systems turn Option-scroll into sideways scrolling.
                            var turned = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                            if ((wheel.modifiers & Qt.AltModifier) && app.hasBackdrop)
                                app.scaleBackdrop(Math.pow(1.1, turned / 120.0))
                            else
                                viewport.zoomBy(turned / 120.0, wheel.x, wheel.y)
                        }
                        onDoubleClicked: viewport.resetView()
                    }

                    // The handles: drag to move, turn and size the selected
                    // layer, or its whole group.
                    Gizmo {
                        id: gizmo
                        anchors.fill: parent
                        visible: handlesButton.checked
                        viewport: viewport
                        threeD: app.threeD
                        target: app.gizmo
                        mode: app.gizmoMode
                        onBegin: function(kind) { app.beginGizmo(kind) }
                        onDrag: function(change) { app.dragGizmo(change) }
                        onEnd: app.endGizmo()
                        onCancel: app.cancelGizmo()
                        onPivotMoved: function(dx, dy, dz) { app.movePivot(dx, dy, dz) }
                        onPicked: function(index, modifiers) { app.clickViewport(index, modifiers) }
                    }

                    // Which handles, and what they turn and size about.
                    Column {
                        objectName: "gizmoBar"
                        visible: handlesButton.checked
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 8
                        anchors.topMargin: 30
                        spacing: 4
                        Row {
                            spacing: 2
                            Repeater {
                                model: [
                                    { key: "move", label: "\u2725 Move", tip: "Move (W): drag the middle, or an arrow to keep to one axis" },
                                    { key: "turn", label: "\u21bb Turn", tip: "Turn (E): drag a ring" },
                                    { key: "size", label: "\u2922 Size", tip: "Size (R): drag the middle for everything, an axis box to stretch" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    implicitHeight: 24
                                    text: modelData.label
                                    tip: modelData.tip
                                    checked: app.gizmoMode === modelData.key
                                    onClicked: app.gizmoMode = modelData.key
                                }
                            }
                        }
                        Row {
                            spacing: 2
                            visible: app.selectionCount > 0
                            Repeater {
                                model: [
                                    { key: "center", label: "Middle", tip: "Turn and size about the middle of everything selected" },
                                    { key: "active", label: "Active", tip: "Turn and size about the layer clicked last" },
                                    { key: "individual", label: "Each", tip: "Turn and size each layer about its own position" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    implicitHeight: 22
                                    quiet: true
                                    text: modelData.label
                                    tip: modelData.tip
                                    checked: app.pivotMode === modelData.key
                                    onClicked: app.pivotMode = modelData.key
                                }
                            }
                            Item { width: 6; height: 1 }
                            VButton {
                                implicitHeight: 22
                                quiet: true
                                text: app.gizmoSpace === "local" ? "Local" : "World"
                                checked: app.gizmoSpace === "local"
                                tip: app.gizmoSpace === "local"
                                     ? "The handles follow the layer's own axes: up is the way it sends particles. Click for the world's axes."
                                     : "The handles follow the world's axes. Click for the layer's own axes (up is the way it sends particles)."
                                onClicked: app.gizmoSpace = app.gizmoSpace === "local" ? "world" : "local"
                            }
                            VButton {
                                implicitHeight: 22
                                quiet: true
                                text: "Reset pivot"
                                tip: "Put the pivot back after Option-dragging it"
                                onClicked: app.resetPivot()
                            }
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 10
                        text: app.particleCount + " particles   " + app.framesPerSecond + " frames per second"
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                    }
                    Row {
                        objectName: "previewBar"
                        anchors.right: parent.right
                        anchors.top: handlesButton.bottom
                        anchors.topMargin: 4
                        anchors.rightMargin: 8
                        spacing: 2
                        Repeater {
                            model: app.hasTarget ? [
                                { key: "composite", label: "Both", tip: "The target with the effect on it" },
                                { key: "effect", label: "Effect", tip: "The effect alone" },
                                { key: "target", label: "Target", tip: "The target alone" }
                            ] : []
                            delegate: VButton {
                                required property var modelData
                                quiet: true
                                implicitHeight: 24
                                text: modelData.label
                                tip: modelData.tip
                                checked: app.previewMode === modelData.key
                                onClicked: app.previewMode = modelData.key
                            }
                        }
                        Item { width: 6; height: 1 }
                        VButton {
                            quiet: true
                            implicitHeight: 24
                            readonly property var order: ["dark", "black", "white", "checker"]
                            readonly property var names: ({ dark: "Dark", black: "Black", white: "White", checker: "Checker" })
                            text: "\u25d0 " + names[app.previewBackground]
                            tip: "The background behind the effect: dark, black, white or a checkerboard for see-through parts. Click to change."
                            onClicked: app.previewBackground = order[(order.indexOf(app.previewBackground) + 1) % order.length]
                        }
                    }
                    VButton {
                        id: handlesButton
                        objectName: "handlesButton"
                        anchors.right: resetViewButton.left
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        anchors.rightMargin: 4
                        quiet: true
                        checked: true
                        text: "Handles"
                        tip: "Show the handles to move, turn and size the selected layer (or its group) by dragging"
                        onClicked: checked = !checked
                    }
                    VButton {
                        id: resetViewButton
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        quiet: true
                        text: "Reset view"
                        tip: app.threeD ? "Drag to look around, Shift-drag to move, scroll to zoom" : "Drag to move, scroll to zoom"
                        onClicked: viewport.resetView()
                    }
                    Text {
                        visible: app.warning.length > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        wrapMode: Text.WordWrap
                        text: app.warning
                        color: theme.error
                        font.pixelSize: theme.smallFontSize
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.line }

                // ---- transport: the scrub bar, then the buttons
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    color: theme.panel

                    VSlider {
                        id: scrub
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        anchors.topMargin: 6
                        height: 22
                        property bool resume: false
                        from: 0
                        to: app.duration
                        value: app.time
                        onDragStarted: {
                            resume = app.playing
                            app.pause()
                        }
                        onMoved: function(v) { app.seek(v) }
                        onDragEnded: if (resume) app.play()
                    }

                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 12
                        anchors.bottomMargin: 9
                        height: 30
                        spacing: 6

                        VButton { text: "Restart"; tip: "Play from the start (Home)"; onClicked: app.restart() }
                        VButton {
                            Layout.preferredWidth: 64
                            primary: true
                            text: app.playing ? "Pause" : "Play"
                            tip: "Play or pause (Space)"
                            onClicked: app.togglePlay()
                        }
                        VButton { Layout.preferredWidth: 30; text: "\u2039"; tip: "Back one frame (,)"; onClicked: app.stepFrames(-1) }
                        VButton { Layout.preferredWidth: 30; text: "\u203a"; tip: "Forward one frame (.)"; onClicked: app.stepFrames(1) }

                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.minimumWidth: 40
                            text: app.time.toFixed(2) + " s  \u00b7  " + (app.frame + 1) + "/" + app.frameCount
                            color: theme.dim
                            font.pixelSize: theme.smallFontSize
                            elide: Text.ElideRight
                        }

                        VCheck {
                            text: "Loop"
                            checked: app.loop
                            onToggled: function(nowChecked) { app.setLoop(nowChecked) }
                        }
                        VButton {
                            objectName: "loopJumpButton"
                            Layout.leftMargin: 4
                            visible: app.loop && app.loopJump !== ""
                            text: "\u26a0 Fix loop jump"
                            tip: app.loopJump
                            onClicked: app.smoothLoop()
                        }
                        VCheck {
                            Layout.leftMargin: 6
                            text: "Sound"
                            checked: app.soundOn
                            enabled: app.soundAvailable
                            onToggled: function(nowChecked) { app.soundOn = nowChecked }
                        }
                        VButton {
                            Layout.preferredWidth: 58
                            Layout.leftMargin: 6
                            readonly property var speeds: [0.25, 0.5, 1, 2]
                            text: app.timeScale + "\u00d7"
                            tip: "Playback speed. Click to change."
                            onClicked: {
                                var at = speeds.indexOf(app.timeScale)
                                app.timeScale = speeds[(at + 1) % speeds.length]
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.line }

            // ---- right: the controls of the selected layer. Simple shows a
            // handful; Advanced shows every setting, in groups that fold away.
            Rectangle {
                id: controlsPanel
                objectName: "controlsPanel"
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: theme.panel

                // Which groups of the Advanced view are folded away. Kept
                // here so it survives the list being rebuilt.
                property var folded: ({})
                property int foldedTick: 0
                function isFolded(section) {
                    return foldedTick >= 0 && section !== undefined && section.length > 0 && folded[section] === true
                }
                function toggleFolded(section) {
                    folded[section] = !(folded[section] === true)
                    foldedTick = foldedTick + 1
                }
                // The list is rebuilt when a value changes form (a number
                // becomes a curve) or something is undone. It should not
                // jump back to the top when that happens, so where it was
                // scrolled to is noted before and put back after, once the
                // new rows have been laid out and the list is tall enough.
                property real savedY: 0
                property real pendingY: -1
                function settle() {
                    if (pendingY < 0)
                        return
                    var furthest = Math.max(0, controlsScroll.contentHeight - controlsScroll.height)
                    controlsScroll.contentY = Math.min(pendingY, furthest)
                    if (furthest >= pendingY)
                        pendingY = -1
                }
                Connections {
                    target: app
                    function onControlsAboutToChange() { controlsPanel.savedY = controlsScroll.contentY }
                    function onControlsPlace(keep) {
                        controlsPanel.pendingY = keep ? controlsPanel.savedY : 0
                        controlsPanel.settle()
                        settleTimer.restart()
                    }
                }
                Timer {
                    id: settleTimer
                    interval: 150
                    onTriggered: {
                        controlsPanel.settle()
                        controlsPanel.pendingY = -1
                    }
                }

                // Scrolls the list so that a group's heading is at the top.
                // True when there is such a group.
                function showSection(section) {
                    for (var i = 0; i < controlsRepeater.count; ++i) {
                        var row = controlsRepeater.itemAt(i)
                        if (row && row.heading && row.modelData.section === section) {
                            var furthest = Math.max(0, controlsScroll.contentHeight - controlsScroll.height)
                            controlsScroll.contentY = Math.min(row.y, furthest)
                            return true
                        }
                    }
                    return false
                }

                // Scrolls the list so that one setting is at the top. True
                // when the list has a setting of that name.
                function showControl(label) {
                    for (var i = 0; i < controlsRepeater.count; ++i) {
                        var row = controlsRepeater.itemAt(i)
                        if (row && !row.heading && row.modelData.label === label) {
                            var furthest = Math.max(0, controlsScroll.contentHeight - controlsScroll.height)
                            controlsScroll.contentY = Math.min(row.y, furthest)
                            return true
                        }
                    }
                    return false
                }

                PanelTitle {
                    id: controlsTitle
                    anchors.left: parent.left
                    anchors.right: viewSwitch.left
                    anchors.top: parent.top
                    anchors.margins: 12
                    text: "Controls"
                    detail: app.selectedLayerName
                }
                Row {
                    id: viewSwitch
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: controlsTitle.verticalCenter
                    spacing: 4
                    VButton {
                        height: 24
                        text: "Simple"
                        tip: "The handful of controls that matter most"
                        checked: !app.advanced
                        onClicked: app.advanced = false
                    }
                    VButton {
                        height: 24
                        text: "Advanced"
                        tip: "Every setting of this layer, with curve and colour-gradient editors"
                        checked: app.advanced
                        onClicked: app.advanced = true
                    }
                }

                Text {
                    visible: app.controls.length === 0
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: controlsTitle.bottom
                    anchors.margins: 12
                    wrapMode: Text.WordWrap
                    color: theme.dim
                    font.pixelSize: theme.fontSize
                    text: app.layers.length === 0
                          ? "This effect has no layers. Add one on the left to begin."
                          : "This layer has no controls."
                }

                Flickable {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: controlsTitle.bottom
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.bottomMargin: 8
                    clip: true
                    id: controlsScroll
                    contentWidth: width
                    contentHeight: controlsColumn.height + 12
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: controlsColumn
                        width: parent.width
                        spacing: 14
                        onHeightChanged: controlsPanel.settle()

                        Repeater {
                            id: controlsRepeater
                            model: app.controls

                            delegate: Loader {
                                id: slot
                                required property var modelData
                                required property int index
                                width: controlsColumn.width
                                readonly property bool heading: modelData.kind === "section"
                                // A folded group shows only its heading.
                                visible: heading || !controlsPanel.isFolded(modelData.section)

                                // The editor is created with its values already in
                                // place, so nothing in it ever sees a missing one.
                                Component.onCompleted: {
                                    var kind = modelData.kind
                                    var file = kind === "number" ? "ControlNumber.qml"
                                             : kind === "shape" ? "ControlShape.qml"
                                             : kind === "choice" ? "ControlChoice.qml"
                                             : kind === "blend" ? "ControlBlend.qml"
                                             : kind === "range" ? "ControlRange.qml"
                                             : kind === "color" ? "ControlColor.qml"
                                             : kind === "direction" ? "ControlDirection.qml"
                                             : kind === "sound" ? "ControlSound.qml"
                                             : kind === "curve" ? "ControlCurve.qml"
                                             : kind === "gradient" ? "ControlGradient.qml"
                                             : kind === "bursts" ? "ControlBursts.qml"
                                             : kind === "vector" ? "ControlVector.qml"
                                             : kind === "flag" ? "ControlFlag.qml"
                                             : kind === "section" ? "ControlSection.qml"
                                             : "ControlNote.qml"
                                    // Which setting this row changes: its place in the
                                    // list of settings, which headings are not part of.
                                    setSource(Qt.resolvedUrl(file), { "info": modelData, "controlIndex": modelData.control })
                                }
                                Binding {
                                    target: slot.item
                                    property: "folded"
                                    value: controlsPanel.isFolded(slot.modelData.section)
                                    when: slot.heading && slot.item !== null
                                }
                                Connections {
                                    target: slot.heading ? slot.item : null
                                    ignoreUnknownSignals: true
                                    function onToggled() { controlsPanel.toggleFolded(slot.modelData.section) }
                                }
                            }
                        }
                    }
                }

                // Shows there is more to scroll to, and where you are in it.
                Rectangle {
                    visible: controlsScroll.contentHeight > controlsScroll.height
                    anchors.right: parent.right
                    anchors.rightMargin: 3
                    width: 4
                    radius: 2
                    color: theme.hover
                    y: controlsScroll.y + controlsScroll.visibleArea.yPosition * controlsScroll.height
                    height: Math.max(24, controlsScroll.visibleArea.heightRatio * controlsScroll.height)
                }
            }
        }

        // ---- status bar
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.line }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: theme.panel

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: versionText.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: app.message
                color: app.messageIsError ? theme.error : theme.dim
                font.pixelSize: theme.smallFontSize
                elide: Text.ElideRight
            }
            Text {
                id: versionText
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "VFX Forge " + app.version
                color: theme.faint
                font.pixelSize: theme.smallFontSize
            }
        }
    }

    // ------------------------------------------------------ the library

    Library {
        id: library
        objectName: "library"
        anchors.fill: parent
        visible: app.libraryOpen
        onCloseRequested: app.libraryOpen = false
        onOpenRequested: function(presetId) {
            root.guard(function() {
                if (app.openPreset(presetId))
                    app.libraryOpen = false
            })
        }
        onAddRequested: function(presetId) {
            if (app.addPreset(presetId))
                app.libraryOpen = false
        }
    }

    // ---------------------------------------------------------- export

    ExportPanel {
        anchors.fill: parent
        visible: app.exportOpen
        viewState: visible ? viewport.viewState() : ({})
        onCloseRequested: app.exportOpen = false
    }

    // ------------------------------------------------ reference to VFX

    ReferenceWorkspace {
        anchors.fill: parent
        visible: reference.open
        onCloseRequested: reference.open = false
    }

    // --------------------------------------------- "save your changes?"

    Rectangle {
        id: unsaved
        visible: false
        anchors.fill: parent
        color: "#b0000000"

        function settle(run) {
            visible = false
            var action = root.pendingAction
            root.pendingAction = null
            if (run && action)
                action()
        }

        // Swallow clicks and scrolling so nothing behind can be touched.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: function(wheel) { wheel.accepted = true }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 420
            height: 150
            radius: 8
            color: theme.panel
            border.color: theme.line

            Text {
                id: unsavedTitle
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                text: "Save your changes first?"
                color: theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: unsavedTitle.bottom
                anchors.margins: 20
                anchors.topMargin: 8
                wrapMode: Text.WordWrap
                text: "“" + app.effectName + "” has changes that have not been saved. If you do not save them, they will be lost."
                color: theme.dim
                font.pixelSize: theme.fontSize
            }
            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                spacing: 8

                VButton { text: "Cancel"; onClicked: unsaved.settle(false) }
                VButton { text: "Don’t Save"; onClicked: unsaved.settle(true) }
                VButton {
                    primary: true
                    text: "Save"
                    onClicked: {
                        var action = root.pendingAction
                        root.pendingAction = null
                        unsaved.visible = false
                        root.saveThen(action)
                    }
                }
            }
        }
    }
}
