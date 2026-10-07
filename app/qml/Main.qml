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
        onTriggered: app.tick(Math.min(frameTime, 0.1))
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

                VButton { text: "New 2D"; tip: "Start a new flat effect"; onClicked: root.guard(function() { app.newEffect(false) }) }
                VButton { text: "New 3D"; tip: "Start a new effect with depth"; onClicked: root.guard(function() { app.newEffect(true) }) }
                VButton { text: "Open…"; tip: "Open a .vfx file"; onClicked: root.guard(function() { openDialog.open() }) }
                VButton { text: "Save"; tip: "Save this effect"; enabled: app.dirty || !app.hasFile; onClicked: root.saveThen(null) }
                VButton { text: "Save As…"; tip: "Save a copy under a new name"; onClicked: root.saveAsThen(null) }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; Layout.leftMargin: 6; Layout.rightMargin: 6; color: theme.line }

                VButton { text: "Undo"; enabled: app.canUndo; tip: app.canUndo ? "Undo: " + app.undoName : "Nothing to undo"; onClicked: app.undo() }
                VButton { text: "Redo"; enabled: app.canRedo; tip: app.canRedo ? "Redo: " + app.redoName : "Nothing to redo"; onClicked: app.redo() }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Advanced and Expert views: NOT IMPLEMENTED yet"
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

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
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
                    PanelTitle { Layout.fillWidth: true; text: "Layers" }

                    ListView {
                        id: layerList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: app.layers
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Rectangle {
                            id: layerRow
                            required property var modelData
                            required property int index
                            readonly property bool selected: index === app.selectedLayer

                            width: layerList.width
                            height: 32
                            radius: 5
                            color: selected ? theme.raised : (rowArea.containsMouse ? theme.hover : "transparent")
                            border.width: selected ? 1 : 0
                            border.color: theme.accent

                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: app.selectLayer(layerRow.index)
                            }
                            VCheck {
                                id: enabledBox
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                checked: layerRow.modelData.enabled
                                onToggled: function(nowChecked) { app.setLayerEnabled(layerRow.index, nowChecked) }
                            }
                            Text {
                                visible: !layerRow.selected
                                anchors.left: enabledBox.right
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: layerRow.modelData.name
                                color: layerRow.modelData.enabled ? theme.text : theme.faint
                                font.pixelSize: theme.fontSize
                                elide: Text.ElideRight
                            }
                            VTextField {
                                visible: layerRow.selected
                                anchors.left: enabledBox.right
                                anchors.leftMargin: 6
                                anchors.right: removeButton.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: layerRow.modelData.name
                                onEdited: function(newText) { app.renameLayer(layerRow.index, newText) }
                            }
                            VButton {
                                id: removeButton
                                visible: layerRow.selected
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                implicitWidth: 26
                                implicitHeight: 24
                                quiet: true
                                text: "×"
                                tip: "Remove this layer"
                                onClicked: app.removeLayer(layerRow.index)
                            }
                        }
                    }

                    VButton {
                        Layout.fillWidth: true
                        text: "Add a layer"
                        tip: "Add another emitter to this effect"
                        onClicked: app.addLayer()
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
                    color: theme.viewport
                    clip: true

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
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        property real lastX: 0
                        property real lastY: 0
                        onPressed: function(mouse) {
                            lastX = mouse.x
                            lastY = mouse.y
                            forceActiveFocus()
                        }
                        onPositionChanged: function(mouse) {
                            var secondary = mouse.buttons !== Qt.LeftButton || (mouse.modifiers & Qt.ShiftModifier)
                            viewport.dragBy(mouse.x - lastX, mouse.y - lastY, secondary ? true : false)
                            lastX = mouse.x
                            lastY = mouse.y
                        }
                        onWheel: function(wheel) {
                            viewport.zoomBy(wheel.angleDelta.y / 120.0, wheel.x, wheel.y)
                        }
                        onDoubleClicked: viewport.resetView()
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 10
                        text: app.particleCount + " particles   " + app.framesPerSecond + " frames per second"
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                    }
                    VButton {
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

                // ---- transport
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: theme.panel

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 12
                        spacing: 6

                        VButton { text: "Restart"; tip: "Play from the start (Home)"; onClicked: app.restart() }
                        VButton {
                            Layout.preferredWidth: 64
                            primary: true
                            text: app.playing ? "Pause" : "Play"
                            tip: "Play or pause (Space)"
                            onClicked: app.togglePlay()
                        }
                        VButton { text: "‹ Frame"; tip: "Back one frame (,)"; onClicked: app.stepFrames(-1) }
                        VButton { text: "Frame ›"; tip: "Forward one frame (.)"; onClicked: app.stepFrames(1) }

                        VSlider {
                            id: scrub
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
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

                        Text {
                            Layout.preferredWidth: 150
                            horizontalAlignment: Text.AlignRight
                            text: app.time.toFixed(2) + " s    frame " + (app.frame + 1) + " of " + app.frameCount
                            color: theme.dim
                            font.pixelSize: theme.smallFontSize
                        }

                        VCheck {
                            Layout.leftMargin: 8
                            text: "Loop"
                            checked: app.loop
                            onToggled: function(nowChecked) { app.setLoop(nowChecked) }
                        }
                        VButton {
                            Layout.preferredWidth: 58
                            readonly property var speeds: [0.25, 0.5, 1, 2]
                            text: app.timeScale + "×"
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

            // ---- right: the Simple controls of the selected layer
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: theme.panel

                PanelTitle {
                    id: controlsTitle
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    text: "Controls"
                    detail: app.selectedLayerName
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
                          : "This layer has no Simple controls."
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
                    contentWidth: width
                    contentHeight: controlsColumn.height
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: controlsColumn
                        width: parent.width
                        spacing: 14

                        Repeater {
                            model: app.controls

                            delegate: Loader {
                                id: slot
                                required property var modelData
                                required property int index
                                width: controlsColumn.width

                                // The editor is created with its values already in
                                // place, so nothing in it ever sees a missing one.
                                Component.onCompleted: {
                                    var kind = modelData.kind
                                    var file = kind === "number" ? "ControlNumber.qml"
                                             : kind === "range" ? "ControlRange.qml"
                                             : kind === "color" ? "ControlColor.qml"
                                             : kind === "direction" ? "ControlDirection.qml"
                                             : "ControlNote.qml"
                                    setSource(Qt.resolvedUrl(file), { "info": modelData, "controlIndex": index })
                                }
                            }
                        }
                    }
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
