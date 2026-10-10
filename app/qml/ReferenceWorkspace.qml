import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import VfxForge 1.0

// Reference to VFX: bring a picture, a GIF or a short video of an effect,
// have it studied, and get an editable effect built from what was found.
Rectangle {
    id: root

    signal closeRequested()

    // "setup" shows the whole reference for cropping; "compare" puts the
    // reference and the effect together.
    property string tab: "setup"
    property string compare: "side"   // side, overlay or difference
    property bool picking: false      // the next click on the picture picks the background colour
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0

    color: theme.window

    function resetView() {
        zoom = 1.0
        panX = 0
        panY = 0
    }

    Connections {
        target: reference
        function onResultChanged() {
            if (reference.built && root.tab === "setup")
                root.tab = "compare"
        }
        function onReferenceChanged() {
            root.resetView()
            if (!reference.built)
                root.tab = "setup"
        }
    }

    // Nothing behind the workspace can be clicked or scrolled.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    FileDialog {
        id: referenceDialog
        title: "Choose a reference picture or clip"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Pictures and clips (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.mp4 *.webm *.mov *.m4v)", "All files (*)"]
        onAccepted: reference.load(selectedFile)
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0)
                reference.load(drop.urls[0])
        }
    }

    // ---- small pieces used all over this page
    component Heading: Text {
        color: theme.dim
        font.pixelSize: theme.smallFontSize
        font.letterSpacing: 1.1
        font.weight: Font.DemiBold
        Layout.fillWidth: true
        Layout.topMargin: 10
    }
    component Note: Text {
        color: theme.dim
        font.pixelSize: theme.smallFontSize
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component Chip: Rectangle {
        property string text: ""
        property bool strong: false
        implicitWidth: chipText.implicitWidth + 12
        implicitHeight: 18
        radius: 9
        color: strong ? "#2a3b2c" : "#3a3326"
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.text
            color: parent.strong ? "#9fd8a4" : "#e3c27a"
            font.pixelSize: 10
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------- top bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 10

                Text {
                    text: "Reference to VFX"
                    color: theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: reference.hasReference ? reference.name + "  ·  " + reference.info
                                                 : "Rebuild an effect you have a picture or a clip of, as layers you can edit."
                    color: theme.dim
                    font.pixelSize: theme.fontSize
                    elide: Text.ElideRight
                }
                VButton {
                    visible: reference.hasReference
                    text: "Another reference…"
                    tip: "Choose a different picture or clip"
                    onClicked: referenceDialog.open()
                }
                VButton {
                    text: reference.built ? "Edit the effect" : "Close"
                    primary: reference.built
                    tip: "Back to the main window (Esc). The effect is already there."
                    onClicked: root.closeRequested()
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.line }

        // ---------------------------------------------- nothing chosen yet
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !reference.hasReference

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80, 620)
                height: 330
                radius: 12
                color: theme.panel
                border.color: theme.line
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 28
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: reference.busy ? reference.stage + "…" : "Drop a picture, a GIF or a short video here"
                        color: theme.text
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        visible: reference.busy
                        radius: 2
                        color: theme.field
                        Rectangle {
                            width: parent.width * reference.progress
                            height: parent.height
                            radius: 2
                            color: theme.accent
                        }
                    }
                    VButton {
                        Layout.alignment: Qt.AlignHCenter
                        primary: true
                        visible: !reference.busy
                        text: "Choose a file…"
                        onClicked: referenceDialog.open()
                    }
                    VButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: reference.busy
                        text: "Stop"
                        onClicked: reference.cancel()
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: reference.problem.length > 0
                        text: reference.problem + (reference.problemDetail.length > 0 ? "\n" + reference.problemDetail : "")
                        color: theme.error
                        font.pixelSize: theme.fontSize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        Layout.fillWidth: true
                        text: reference.formats
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "One effect per reference works best: you can crop to it. Clips up to 12 seconds are read.\n"
                              + "Everything is worked out on this computer. Nothing is uploaded, and no AI model is downloaded or needed."
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // -------------------------------------------------- the workspace
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: reference.hasReference
            spacing: 0

            // ---- left: how to read it, and how to rebuild it
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: theme.panel

                Flickable {
                    id: leftFlick
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: width
                    contentHeight: leftColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: leftColumn
                        width: leftFlick.width - 10
                        spacing: 6

                        Heading { text: "1 · READING THE REFERENCE"; Layout.topMargin: 0 }

                        Note { text: "What is behind the effect" }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "auto", label: "Work it out" },
                                    { key: "transparent", label: "See-through" },
                                    { key: "dark", label: "Dark" },
                                    { key: "light", label: "Light" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    checked: reference.backdrop === modelData.key
                                    onClicked: { root.picking = false; reference.backdrop = modelData.key }
                                }
                            }
                            VButton {
                                text: root.picking ? "Click the picture…" : "Pick a colour"
                                checked: reference.backdrop === "colour" || root.picking
                                tip: "Then click the background in the picture"
                                onClicked: { root.tab = "setup"; root.picking = !root.picking }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: reference.backdrop === "colour"
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                radius: 4
                                color: reference.keyColour
                                border.color: theme.line
                            }
                            Note { text: "This colour is taken as the background." }
                        }

                        Note { text: "The part to look at"; Layout.topMargin: 6 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            VButton {
                                text: "Crop…"
                                checked: root.tab === "setup"
                                tip: "Show the whole picture and drag a box round the effect"
                                onClicked: { root.picking = false; root.tab = "setup" }
                            }
                            VButton {
                                text: "Whole picture"
                                enabled: reference.cropLeft > 0 || reference.cropTop > 0 || reference.cropRight < 1 || reference.cropBottom < 1
                                onClicked: reference.resetCrop()
                            }
                        }

                        Note { text: "Faintest part that still counts"; Layout.topMargin: 6 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            VCheck {
                                text: "Decide for me"
                                checked: reference.cutoff < 0
                                onToggled: function(nowChecked) { reference.cutoff = nowChecked ? -1 : 0.06 }
                            }
                            VSlider {
                                Layout.fillWidth: true
                                enabled: reference.cutoff >= 0
                                opacity: enabled ? 1 : 0.35
                                from: 0
                                to: 0.5
                                value: Math.max(0, reference.cutoff)
                                onMoved: function(newValue) { reference.cutoff = newValue }
                            }
                        }

                        Note { text: "How closely to look"; Layout.topMargin: 6 }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "quick", label: "Quick", tip: "For a slow laptop: a coarser look" },
                                    { key: "normal", label: "Normal", tip: "" },
                                    { key: "fine", label: "Fine", tip: "Slower: small sparks and thin rays are picked up better" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    tip: modelData.tip
                                    checked: reference.detail === modelData.key
                                    onClicked: reference.detail = modelData.key
                                }
                            }
                        }

                        // ---- only for clips
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: reference.moving
                            spacing: 6

                            Note {
                                Layout.topMargin: 6
                                text: "Start at frame " + (reference.firstFrame + 1) + ", end at frame " + (reference.lastFrame + 1)
                                      + " of " + reference.frameCount
                            }
                            VSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(1, reference.frameCount - 1)
                                value: reference.firstFrame
                                onMoved: function(newValue) { reference.firstFrame = Math.round(newValue) }
                            }
                            VSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(1, reference.frameCount - 1)
                                value: reference.lastFrame
                                onMoved: function(newValue) { reference.lastFrame = Math.round(newValue) }
                            }
                            Note { text: "Playback speed: " + reference.speed.toFixed(2) + " × the clip" }
                            VSlider {
                                Layout.fillWidth: true
                                from: 0.25
                                to: 4
                                value: reference.speed
                                onMoved: function(newValue) { reference.speed = Math.round(newValue * 20) / 20 }
                            }
                            Note {
                                text: "The clip is read at " + reference.sourceRate.toFixed(1)
                                      + " frames a second. For a video, how many frames a second to take:"
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: [10, 15, 24, 30]
                                    delegate: VButton {
                                        required property int modelData
                                        text: "" + modelData
                                        checked: Math.round(reference.sampling) === modelData
                                        tip: "Reads the video again"
                                        onClicked: reference.sampling = modelData
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 10; color: theme.line }
                        Heading { text: "2 · REBUILDING IT" }

                        Note { text: "What to favour" }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "balanced", label: "Balanced" },
                                    { key: "shape", label: "Shape and colour" },
                                    { key: "motion", label: "Motion" },
                                    { key: "layered", label: "Layered" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    checked: reference.mode === modelData.key
                                    onClicked: reference.mode = modelData.key
                                }
                            }
                        }
                        Note {
                            text: reference.mode === "shape" ? "Looks as much like the picture as it can, and holds that look longer."
                                : reference.mode === "motion" ? "Follows the clip's timing and movement first, with fewer layers. Best with a GIF or a video."
                                : reference.mode === "layered" ? "Splits into as many separate layers as were found, so each part can be edited alone."
                                : "A sensible middle between look, movement, ease of editing and cost. Start here."
                        }

                        Note { text: "Which take"; Layout.topMargin: 6 }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "closest", label: "Closest" },
                                    { key: "performance", label: "Lighter" },
                                    { key: "enhanced", label: "Enhanced" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    checked: reference.variation === modelData.key
                                    onClicked: reference.variation = modelData.key
                                }
                            }
                        }
                        Note {
                            text: reference.variation === "performance" ? "Fewer particles and at most four layers, for slower devices."
                                : reference.variation === "enhanced" ? "Adds glints and an afterglow the reference does not have. They are marked as added, and you choose this take yourself: it never replaces the closest one."
                                : "As near the reference as the reading allows."
                        }

                        Note { text: "Where it will run"; Layout.topMargin: 6 }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "mobile", label: "Mobile", tip: "At most about 120 particles at once" },
                                    { key: "desktop", label: "Desktop", tip: "At most about 600 particles at once" },
                                    { key: "vr", label: "VR", tip: "At most about 250 particles at once" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    tip: modelData.tip
                                    checked: reference.target === modelData.key
                                    onClicked: reference.target = modelData.key
                                }
                            }
                        }

                        Note { text: "One burst, or something that keeps going"; Layout.topMargin: 6 }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "auto", label: "Work it out" },
                                    { key: "burst", label: "One burst" },
                                    { key: "steady", label: "Keeps going" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    checked: reference.pace === modelData.key
                                    onClicked: reference.pace = modelData.key
                                }
                            }
                        }

                        VCheck {
                            Layout.topMargin: 8
                            text: "Use pieces cut from the reference"
                            checked: reference.cutouts
                            onToggled: function(nowChecked) { reference.cutouts = nowChecked }
                        }
                        Note {
                            text: "Rays and hand-drawn shapes that no built-in shape matches are lifted out of the reference and drawn by a layer. Closer, but those are the reference's own pixels: only do this with art that is yours to use."
                        }

                        Note { text: "What matters most when matching"; Layout.topMargin: 8 }
                        Repeater {
                            model: [
                                { key: "silhouette", label: "Outline and shape" },
                                { key: "colour", label: "Colour" },
                                { key: "brightness", label: "Brightness and glow" },
                                { key: "density", label: "Number of pieces" },
                                { key: "motion", label: "Motion" },
                                { key: "timing", label: "Timing" },
                                { key: "detail", label: "Fine detail" }
                            ]
                            delegate: RowLayout {
                                id: priorityRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.preferredWidth: 118
                                    text: priorityRow.modelData.label
                                    color: theme.text
                                    font.pixelSize: theme.smallFontSize
                                    elide: Text.ElideRight
                                }
                                VSlider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 2
                                    value: reference.priorities[priorityRow.modelData.key] === undefined ? 1 : reference.priorities[priorityRow.modelData.key]
                                    onMoved: function(newValue) { reference.setPriority(priorityRow.modelData.key, Math.round(newValue * 4) / 4) }
                                }
                            }
                        }
                        Note { text: "Left is “ignore”, the middle is normal. Sizes and brightness are adjusted to suit; the measurement below is weighted the same way." }

                        Note { text: "How hard to try"; Layout.topMargin: 6 }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "quick", label: "Quick" },
                                    { key: "normal", label: "Normal" },
                                    { key: "thorough", label: "Thorough" }
                                ]
                                delegate: VButton {
                                    required property var modelData
                                    text: modelData.label
                                    checked: reference.quality === modelData.key
                                    onClicked: reference.quality = modelData.key
                                }
                            }
                        }

                        VButton {
                            Layout.fillWidth: true
                            Layout.topMargin: 12
                            Layout.preferredHeight: 34
                            primary: true
                            enabled: !reference.busy
                            text: reference.built ? "Build again" : "Build the effect"
                            tip: "Replaces the open effect's layers, as one undo step. Locked layers stay."
                            onClicked: reference.build()
                        }
                        Item { Layout.preferredHeight: 12 }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.line }

            // ---- middle: the pictures, the timeline, the measurements
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Which pictures to show.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: theme.window
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        VButton { text: "Whole reference"; checked: root.tab === "setup"; onClicked: root.tab = "setup" }
                        VButton {
                            text: "Side by side"
                            checked: root.tab === "compare" && root.compare === "side"
                            onClicked: { root.tab = "compare"; root.compare = "side" }
                        }
                        VButton {
                            text: "Overlay"
                            enabled: reference.built
                            checked: root.tab === "compare" && root.compare === "overlay"
                            onClicked: { root.tab = "compare"; root.compare = "overlay" }
                        }
                        VButton {
                            text: "Difference"
                            enabled: reference.built
                            checked: root.tab === "compare" && root.compare === "difference"
                            onClicked: { root.tab = "compare"; root.compare = "difference" }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: root.tab === "compare" && root.compare === "overlay"
                            text: "Reference"
                            color: theme.dim
                            font.pixelSize: theme.smallFontSize
                        }
                        VSlider {
                            visible: root.tab === "compare" && root.compare === "overlay"
                            Layout.preferredWidth: 140
                            from: 0
                            to: 1
                            value: reference.overlayOpacity
                            onMoved: function(newValue) { reference.overlayOpacity = newValue }
                        }
                        Text {
                            visible: root.tab === "compare" && root.compare === "overlay"
                            text: "Effect"
                            color: theme.dim
                            font.pixelSize: theme.smallFontSize
                        }
                        VButton {
                            visible: root.tab === "compare" && root.zoom > 1.001
                            text: "Fit"
                            tip: "Zoom back out"
                            onClicked: root.resetView()
                        }
                    }
                }

                // The pictures.
                Rectangle {
                    id: stage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: theme.viewport
                    clip: true

                    // ---- the whole reference, for cropping and picking
                    Item {
                        id: setup
                        anchors.fill: parent
                        anchors.margins: 16
                        visible: root.tab === "setup"

                        readonly property real fa: Math.max(0.05, reference.fullAspect)
                        readonly property real pw: (width / Math.max(1, height) > fa) ? height * fa : width
                        readonly property real ph: (width / Math.max(1, height) > fa) ? height : width / fa
                        readonly property real px: (width - pw) / 2
                        readonly property real py: (height - ph) / 2
                        property bool dragging: false
                        property real dragX0: 0
                        property real dragY0: 0
                        property real dragX1: 0
                        property real dragY1: 0
                        readonly property real boxL: dragging ? Math.min(dragX0, dragX1) : reference.cropLeft
                        readonly property real boxT: dragging ? Math.min(dragY0, dragY1) : reference.cropTop
                        readonly property real boxR: dragging ? Math.max(dragX0, dragX1) : reference.cropRight
                        readonly property real boxB: dragging ? Math.max(dragY0, dragY1) : reference.cropBottom

                        ReferenceView {
                            anchors.fill: parent
                            source: reference
                            kind: "full"
                        }
                        // What is outside the crop is dimmed.
                        Rectangle { x: setup.px; y: setup.py; width: setup.pw; height: setup.boxT * setup.ph; color: "#99000000" }
                        Rectangle { x: setup.px; y: setup.py + setup.boxB * setup.ph; width: setup.pw; height: (1 - setup.boxB) * setup.ph; color: "#99000000" }
                        Rectangle { x: setup.px; y: setup.py + setup.boxT * setup.ph; width: setup.boxL * setup.pw; height: (setup.boxB - setup.boxT) * setup.ph; color: "#99000000" }
                        Rectangle { x: setup.px + setup.boxR * setup.pw; y: setup.py + setup.boxT * setup.ph; width: (1 - setup.boxR) * setup.pw; height: (setup.boxB - setup.boxT) * setup.ph; color: "#99000000" }
                        Rectangle {
                            x: setup.px + setup.boxL * setup.pw
                            y: setup.py + setup.boxT * setup.ph
                            width: (setup.boxR - setup.boxL) * setup.pw
                            height: (setup.boxB - setup.boxT) * setup.ph
                            color: "transparent"
                            border.width: 1
                            border.color: theme.accent
                        }
                        MouseArea {
                            x: setup.px
                            y: setup.py
                            width: setup.pw
                            height: setup.ph
                            cursorShape: Qt.CrossCursor
                            function unit(v, size) { return Math.max(0, Math.min(1, v / Math.max(1, size))) }
                            onPressed: function(mouse) {
                                if (root.picking)
                                    return
                                setup.dragX0 = unit(mouse.x, width)
                                setup.dragY0 = unit(mouse.y, height)
                                setup.dragX1 = setup.dragX0
                                setup.dragY1 = setup.dragY0
                                setup.dragging = true
                            }
                            onPositionChanged: function(mouse) {
                                if (setup.dragging) {
                                    setup.dragX1 = unit(mouse.x, width)
                                    setup.dragY1 = unit(mouse.y, height)
                                }
                            }
                            onReleased: function(mouse) {
                                if (root.picking) {
                                    reference.pickBackdropAt(unit(mouse.x, width), unit(mouse.y, height))
                                    root.picking = false
                                    return
                                }
                                if (!setup.dragging)
                                    return
                                var l = Math.min(setup.dragX0, setup.dragX1), r = Math.max(setup.dragX0, setup.dragX1)
                                var t = Math.min(setup.dragY0, setup.dragY1), b = Math.max(setup.dragY0, setup.dragY1)
                                setup.dragging = false
                                // A click without a drag leaves the crop as it was.
                                if (r - l > 0.03 && b - t > 0.03)
                                    reference.setCrop(l, t, r, b)
                            }
                            onCanceled: setup.dragging = false
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            text: root.picking ? "Click on the background."
                                               : "Drag a box round the one effect you want. Everything outside it is ignored."
                            color: theme.text
                            font.pixelSize: theme.fontSize
                            style: Text.Outline
                            styleColor: "#000000"
                        }
                    }

                    // ---- the reference and the effect together
                    Item {
                        id: compareArea
                        anchors.fill: parent
                        anchors.margins: 12
                        visible: root.tab === "compare"

                        Row {
                            anchors.fill: parent
                            spacing: 8
                            visible: root.compare === "side"
                            Item {
                                width: (parent.width - 8) / 2
                                height: parent.height
                                clip: true
                                ReferenceView { anchors.fill: parent; source: reference; kind: "reference"; zoom: root.zoom; panX: root.panX; panY: root.panY }
                                Text { anchors.left: parent.left; anchors.top: parent.top; text: "Reference"; color: theme.text; font.pixelSize: theme.smallFontSize; style: Text.Outline; styleColor: "#000000" }
                            }
                            Item {
                                width: (parent.width - 8) / 2
                                height: parent.height
                                clip: true
                                ReferenceView { anchors.fill: parent; source: reference; kind: "made"; zoom: root.zoom; panX: root.panX; panY: root.panY }
                                Text { anchors.left: parent.left; anchors.top: parent.top; text: "Your effect"; color: theme.text; font.pixelSize: theme.smallFontSize; style: Text.Outline; styleColor: "#000000" }
                                Text {
                                    anchors.centerIn: parent
                                    visible: !reference.built
                                    width: parent.width - 40
                                    text: reference.busy ? reference.stage + "…" : "Press “Build the effect” on the left to see it here."
                                    color: theme.dim
                                    font.pixelSize: theme.fontSize
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        ReferenceView {
                            anchors.fill: parent
                            visible: root.compare !== "side"
                            source: reference
                            kind: root.compare === "difference" ? "difference" : "overlay"
                            zoom: root.zoom
                            panX: root.panX
                            panY: root.panY
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            visible: root.compare === "difference"
                            text: "Black: the same. Warm: your effect is brighter there. Cool: it is dimmer."
                            color: theme.text
                            font.pixelSize: theme.smallFontSize
                            style: Text.Outline
                            styleColor: "#000000"
                        }
                        // Wheel to zoom, drag to move: both pictures together.
                        MouseArea {
                            anchors.fill: parent
                            property real lastX: 0
                            property real lastY: 0
                            onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
                            onPositionChanged: function(mouse) {
                                if (pressed && root.zoom > 1.001) {
                                    root.panX += mouse.x - lastX
                                    root.panY += mouse.y - lastY
                                }
                                lastX = mouse.x
                                lastY = mouse.y
                            }
                            onWheel: function(wheel) {
                                var next = Math.max(1, Math.min(16, root.zoom * (wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2)))
                                root.zoom = next
                                if (next <= 1.001) {
                                    root.panX = 0
                                    root.panY = 0
                                }
                                wheel.accepted = true
                            }
                        }
                    }

                    // Work under way.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 34
                        visible: reference.busy
                        color: "#dd15161a"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 10
                            Text { text: reference.stage + "…"; color: theme.text; font.pixelSize: theme.fontSize }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 4
                                radius: 2
                                color: theme.field
                                Rectangle { width: parent.width * reference.progress; height: parent.height; radius: 2; color: theme.accent }
                            }
                            VButton { text: "Stop"; onClicked: reference.cancel() }
                        }
                    }
                }

                // A problem, said plainly.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: problemText.implicitHeight + 14
                    visible: reference.problem.length > 0
                    color: "#3a1f1f"
                    Text {
                        id: problemText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 12
                        text: reference.problem + (reference.problemDetail.length > 0 ? "  " + reference.problemDetail : "")
                        color: theme.error
                        font.pixelSize: theme.fontSize
                        wrapMode: Text.WordWrap
                    }
                }

                // ---- the two timelines, scrubbed together
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    color: theme.panel

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 4
                        anchors.bottomMargin: 6
                        spacing: 0

                        // Moments in the reference.
                        Item {
                            id: referenceMarks
                            Layout.fillWidth: true
                            Layout.preferredHeight: 15
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                visible: reference.referenceMarkers.length === 0
                                text: reference.moving ? "Reference" : "Reference: a still picture, so it has no moments of its own"
                                color: theme.faint
                                font.pixelSize: 10
                            }
                            Repeater {
                                model: reference.referenceMarkers
                                delegate: Item {
                                    id: markA
                                    required property var modelData
                                    x: 7 + (referenceMarks.width - 14) * Math.max(0, Math.min(1, modelData.time / Math.max(0.001, reference.length))) - 4
                                    width: 8
                                    height: referenceMarks.height
                                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; width: 2; height: 9; color: theme.accent }
                                    Text {
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 1
                                        x: 6
                                        text: markA.modelData.label
                                        color: theme.accent
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                        VSlider {
                            id: scrub
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(0.001, reference.length)
                            value: reference.time
                            onMoved: function(newValue) { reference.time = newValue }
                        }
                        // Moments in the effect.
                        Item {
                            id: effectMarks
                            Layout.fillWidth: true
                            Layout.preferredHeight: 13
                            Repeater {
                                model: reference.effectMarkers
                                delegate: Rectangle {
                                    required property var modelData
                                    x: 7 + (effectMarks.width - 14) * Math.max(0, Math.min(1, modelData.time / Math.max(0.001, reference.length))) - 1
                                    y: 0
                                    width: 2
                                    height: 8
                                    color: theme.dim
                                    ToolTip.visible: markHover.hovered
                                    ToolTip.text: modelData.label
                                    HoverHandler { id: markHover }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            VButton {
                                Layout.preferredWidth: 64
                                text: reference.playing ? "Pause" : "Play"
                                tip: "Plays the reference and the effect together"
                                onClicked: reference.togglePlay()
                            }
                            VButton { Layout.preferredWidth: 30; text: "‹"; tip: "Back one frame"; onClicked: reference.stepFrames(-1) }
                            VButton { Layout.preferredWidth: 30; text: "›"; tip: "Forward one frame"; onClicked: reference.stepFrames(1) }
                            Text {
                                text: reference.time.toFixed(2) + " s of " + reference.length.toFixed(2)
                                      + (reference.moving ? "  ·  frame " + (reference.frame + 1) : "")
                                color: theme.dim
                                font.pixelSize: theme.smallFontSize
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Orange ticks: moments in the reference. Grey ticks: where the effect's layers start and burst."
                                color: theme.faint
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                Layout.maximumWidth: 480
                            }
                        }
                    }
                }

                // ---- how alike they measure
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: measureColumn.implicitHeight + 16
                    visible: reference.built
                    color: theme.window

                    ColumnLayout {
                        id: measureColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text {
                                text: reference.similarity.overall === undefined ? "–"
                                    : Math.round(reference.similarity.overall * 100) + "%"
                                color: theme.text
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "measured alike. A guide for finding the biggest differences, not a judgement of how alike they look to you. Trust your eye first."
                                color: theme.dim
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                            VButton {
                                text: "Measure again"
                                enabled: !reference.busy
                                tip: "After changing the effect by hand"
                                onClicked: reference.measure()
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 14
                            Repeater {
                                model: [
                                    { key: "silhouette", label: "Outline" },
                                    { key: "colour", label: "Colour" },
                                    { key: "brightness", label: "Brightness" },
                                    { key: "density", label: "Pieces" },
                                    { key: "detail", label: "Detail" },
                                    { key: "motion", label: "Motion" },
                                    { key: "timing", label: "Timing" }
                                ]
                                delegate: Column {
                                    id: bar
                                    required property var modelData
                                    readonly property real amount: reference.similarity[modelData.key] === undefined ? -1 : reference.similarity[modelData.key]
                                    spacing: 2
                                    Text {
                                        text: bar.modelData.label + (bar.amount < 0 ? ": not measured" : ": " + Math.round(bar.amount * 100) + "%")
                                        color: bar.amount < 0 ? theme.faint : theme.text
                                        font.pixelSize: theme.smallFontSize
                                    }
                                    Rectangle {
                                        width: 96
                                        height: 4
                                        radius: 2
                                        color: theme.field
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(1, bar.amount))
                                            height: parent.height
                                            radius: 2
                                            color: bar.amount > 0.8 ? "#7fcf8a" : (bar.amount > 0.6 ? theme.accent : theme.error)
                                        }
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: reference.differences
                            delegate: Text {
                                required property string modelData
                                Layout.fillWidth: true
                                text: "•  " + modelData
                                color: theme.text
                                font.pixelSize: theme.smallFontSize
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.line }

            // ---- right: what was found, the layers, refining, versions
            Rectangle {
                Layout.preferredWidth: 350
                Layout.fillHeight: true
                color: theme.panel

                Flickable {
                    id: rightFlick
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: width
                    contentHeight: rightColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: rightColumn
                        width: rightFlick.width - 10
                        spacing: 6

                        // ---- after building
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: reference.built
                            spacing: 6

                            Heading { text: "THE EFFECT"; Layout.topMargin: 0 }
                            Text {
                                Layout.fillWidth: true
                                visible: reference.said.length > 0
                                text: reference.said
                                color: theme.text
                                font.pixelSize: theme.fontSize
                                wrapMode: Text.WordWrap
                            }

                            Repeater {
                                model: app.layers
                                delegate: Rectangle {
                                    id: layerCard
                                    required property var modelData
                                    required property int index
                                    readonly property var about: reference.layerNotes[modelData.id]
                                    Layout.fillWidth: true
                                    implicitHeight: layerBody.implicitHeight + 12
                                    radius: 6
                                    color: theme.raised

                                    ColumnLayout {
                                        id: layerBody
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 6
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            VCheck {
                                                checked: layerCard.modelData.enabled
                                                onToggled: function(nowChecked) { app.setLayerEnabled(layerCard.index, nowChecked); reference.measure() }
                                            }
                                            VTextField {
                                                Layout.fillWidth: true
                                                text: layerCard.modelData.name
                                                onEdited: function(newText) { app.renameLayer(layerCard.index, newText) }
                                            }
                                            VButton { implicitWidth: 24; implicitHeight: 24; quiet: true; text: "↑"; tip: "Draw it earlier (further back)"; onClicked: { app.moveLayer(layerCard.index, -1); reference.measure() } }
                                            VButton { implicitWidth: 24; implicitHeight: 24; quiet: true; text: "↓"; tip: "Draw it later (in front)"; onClicked: { app.moveLayer(layerCard.index, 1); reference.measure() } }
                                            VButton { implicitWidth: 24; implicitHeight: 24; quiet: true; text: "+"; tip: "Make a copy of this layer"; onClicked: { app.duplicateLayer(layerCard.index); reference.measure() } }
                                            VButton {
                                                implicitHeight: 24
                                                text: layerCard.modelData.locked ? "Locked" : "Lock"
                                                checked: layerCard.modelData.locked
                                                tip: "A locked layer is left alone by Refine, by matching, and by building again"
                                                onClicked: app.setLayerLocked(layerCard.index, !layerCard.modelData.locked)
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            Chip { visible: layerCard.modelData.role.length > 0; text: layerCard.modelData.role; strong: true }
                                            Chip {
                                                visible: layerCard.about !== undefined
                                                text: layerCard.about !== undefined && layerCard.about.added ? "added, not in the reference"
                                                    : (layerCard.about !== undefined && layerCard.about.lookSeen ? "look: seen" : "look: assumed")
                                                strong: layerCard.about !== undefined && layerCard.about.lookSeen && !layerCard.about.added
                                            }
                                            Chip {
                                                visible: layerCard.about !== undefined && !layerCard.about.added
                                                text: layerCard.about !== undefined && layerCard.about.motionSeen ? "motion: seen" : "motion: assumed"
                                                strong: layerCard.about !== undefined && layerCard.about.motionSeen
                                            }
                                            Item { Layout.fillWidth: true }
                                            VButton {
                                                implicitHeight: 22
                                                quiet: true
                                                text: "Edit…"
                                                tip: "Open this layer's controls in the main window"
                                                onClicked: { app.selectLayer(layerCard.index); root.closeRequested() }
                                            }
                                        }
                                        Note {
                                            visible: layerCard.about !== undefined
                                            text: layerCard.about !== undefined ? layerCard.about.technique + (layerCard.about.note.length > 0 ? ". " + layerCard.about.note : "") : ""
                                        }
                                    }
                                }
                            }

                            Heading { text: "REFINE" }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: reference.refinements
                                    delegate: VButton {
                                        required property var modelData
                                        text: modelData.label
                                        tip: modelData.help
                                        enabled: !reference.busy && (!modelData.needsClip || reference.moving)
                                        onClicked: reference.refine(modelData.key)
                                    }
                                }
                                VButton {
                                    text: "Match again"
                                    tip: "Adjust sizes and brightness of unlocked layers to measure closer"
                                    enabled: !reference.busy
                                    onClicked: reference.fitAgain(false)
                                }
                                VButton {
                                    text: "Improve the outline"
                                    tip: "Adjust sizes only, judged by the outline alone"
                                    enabled: !reference.busy
                                    onClicked: reference.fitAgain(true)
                                }
                            }
                            Note { text: "Each of these changes ordinary settings of the layers and is one undo step. Hover for what it does." }
                            Repeater {
                                model: reference.adjustments
                                delegate: Note {
                                    required property string modelData
                                    text: "Adjusted — " + modelData
                                }
                            }

                            Heading { text: "VERSIONS" }
                            Repeater {
                                model: reference.versions
                                delegate: RowLayout {
                                    id: versionRow
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 6
                                    VButton {
                                        Layout.fillWidth: true
                                        text: versionRow.modelData.label + "  ·  " + Math.round(versionRow.modelData.overall * 100) + "%  ·  "
                                              + versionRow.modelData.layers + " layers"
                                        checked: versionRow.modelData.current
                                        enabled: !reference.busy
                                        tip: "Bring this version back (one undo step)"
                                        onClicked: reference.restoreVersion(versionRow.index)
                                    }
                                }
                            }
                            Note { text: "Kept while this window is open, to go back and compare. Undo and Redo work as usual too." }

                            Heading { text: "COST" }
                            Note { text: reference.costs }

                            Heading { text: "WHAT COULD NOT BE REBUILT" }
                            Repeater {
                                model: reference.notes
                                delegate: Note {
                                    required property string modelData
                                    text: "•  " + modelData
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 8; color: theme.line }
                        }

                        // ---- the report
                        Heading { text: "WHAT WAS FOUND"; Layout.topMargin: reference.built ? 10 : 0 }
                        Note {
                            visible: !reference.analysed
                            text: reference.busy ? reference.stage + "…" : "Not studied yet."
                        }
                        Row {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: reference.palette.length > 0
                            Repeater {
                                model: reference.palette
                                delegate: Rectangle {
                                    required property var modelData
                                    width: 26
                                    height: 18
                                    radius: 3
                                    color: modelData.colour
                                    border.color: theme.line
                                    ToolTip.visible: swatchHover.hovered
                                    ToolTip.text: modelData.name + " " + modelData.colour + ", " + Math.round(modelData.share * 100) + "%"
                                    HoverHandler { id: swatchHover }
                                }
                            }
                        }
                        Repeater {
                            model: reference.findings
                            delegate: ColumnLayout {
                                id: found
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 1
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        text: found.modelData.label
                                        color: theme.dim
                                        font.pixelSize: theme.smallFontSize
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 44
                                        Layout.preferredHeight: 4
                                        radius: 2
                                        color: theme.field
                                        Rectangle {
                                            width: parent.width * found.modelData.confidence
                                            height: parent.height
                                            radius: 2
                                            color: found.modelData.confidence > 0.6 ? "#7fcf8a" : (found.modelData.confidence > 0.35 ? theme.accent : theme.error)
                                        }
                                        ToolTip.visible: sureHover.hovered
                                        ToolTip.text: "How sure: " + Math.round(found.modelData.confidence * 100) + "%"
                                        HoverHandler { id: sureHover }
                                    }
                                    Chip { text: found.modelData.seen ? "seen" : "assumed"; strong: found.modelData.seen }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: found.modelData.value
                                    color: theme.text
                                    font.pixelSize: theme.fontSize
                                    wrapMode: Text.WordWrap
                                }
                                Note {
                                    visible: found.modelData.note.length > 0
                                    text: found.modelData.note
                                    font.italic: true
                                }
                                Item { Layout.preferredHeight: 4 }
                            }
                        }

                        Heading { visible: reference.uncertain.length > 0; text: "CANNOT BE TOLD FROM THIS REFERENCE" }
                        Repeater {
                            model: reference.uncertain
                            delegate: Note {
                                required property string modelData
                                text: "•  " + modelData
                            }
                        }
                        Heading { visible: reference.methods.length > 0; text: "HOW IT WAS WORKED OUT" }
                        Repeater {
                            model: reference.methods
                            delegate: Note {
                                required property string modelData
                                text: "•  " + modelData
                            }
                        }

                        Heading { text: "NOT IN THIS VERSION" }
                        Note {
                            text: "•  Following single particles through a clip, and correcting that by hand.\n"
                                + "•  Editing the curves on a timeline (the curves are made and saved; the Advanced and Expert editors to change them are not built yet).\n"
                                + "•  Optional AI models for cutting out busy backgrounds and following motion. None is used or downloaded.\n"
                                + "•  Heat haze and distortion, swirling motion, beams and cones as meshes, depth.\n"
                                + "•  Parts of a burst that only show for a moment in the middle of it: the opening flash and the main part are read; anything else brief is not."
                        }
                        Item { Layout.preferredHeight: 12 }
                    }
                }
            }
        }
    }
}
