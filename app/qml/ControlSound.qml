import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// The selected layer's sound: add one (your own, or from the library), see
// its waveform, and set when and how it plays.
Item {
    id: root

    property var info
    property int controlIndex: -1
    readonly property var sound: info.sound || ({ has: false })

    // Kept here so the panel answers at once; the app is told on each change.
    property string play: sound.play || "start"
    property bool loop: sound.loop || false
    property bool mute: sound.mute || false
    property bool showMore: false

    implicitHeight: column.implicitHeight

    FileDialog {
        id: soundDialog
        title: "Choose a sound for this layer"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Sounds (*.wav *.wave *.mp3 *.ogg *.oga *.flac *.aif *.aiff *.aifc *.m4a)", "All files (*)"]
        onAccepted: app.useSoundFile(selectedFile)
    }

    // The library: every built-in sound, by category, each with a preview.
    Popup {
        id: library
        width: Math.min(360, root.width + 40)
        height: 380
        x: -20
        y: column.height > 0 ? 40 : 0
        modal: true
        padding: 10
        background: Rectangle { color: theme.panel; border.color: theme.line; radius: 8 }
        onClosed: app.stopPreview()

        ListView {
            anchors.fill: parent
            clip: true
            model: app.librarySounds
            spacing: 2
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool first: index === 0 || app.librarySounds[index - 1].category !== modelData.category
                width: ListView.view.width
                height: (first ? 22 : 0) + 34
                Text {
                    visible: row.first
                    y: 4
                    text: row.modelData.category
                    color: theme.faint
                    font.pixelSize: theme.smallFontSize
                }
                Rectangle {
                id: rowBody
                y: row.first ? 22 : 0
                width: parent.width
                height: 34
                radius: 5
                color: rowArea.containsMouse ? theme.raised : "transparent"
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        app.useLibrarySound(row.modelData.id)
                        library.close()
                    }
                }
                VButton {
                    id: listen
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    text: "▶"
                    quiet: true
                    tip: "Listen"
                    onClicked: app.previewLibrarySound(row.modelData.id)
                }
                Column {
                    anchors.left: listen.right
                    anchors.leftMargin: 6
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: row.modelData.name + (row.modelData.loops ? "  (loops)" : "")
                        color: theme.text
                        font.pixelSize: theme.fontSize
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.description
                        color: theme.faint
                        font.pixelSize: theme.smallFontSize
                        elide: Text.ElideRight
                    }
                }
                }
            }
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: 8

        ControlHeader {
            width: parent.width
            label: "Sound"
            unit: root.sound.has ? (root.mute ? "muted" : "") : "none"
        }

        // No sound yet.
        Column {
            visible: !root.sound.has
            width: parent.width
            spacing: 6
            Flow {
                width: parent.width
                spacing: 6
                VButton {
                    text: "Sound library…"
                    primary: true
                    tip: "Pick a ready-made sound. Each one can be heard before you choose it."
                    onClicked: library.open()
                }
                VButton {
                    text: app.importingSound ? "Reading…" : "My own sound…"
                    enabled: !app.importingSound
                    tip: "WAV, MP3, OGG, FLAC or AIFF. A copy is kept with the effect; your file is never changed."
                    onClicked: soundDialog.open()
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "A sound plays when this layer starts, or with every burst of its particles, always in step with the picture."
                color: theme.faint
                font.pixelSize: theme.smallFontSize
            }
        }

        // A sound is set.
        Column {
            visible: root.sound.has
            width: parent.width
            spacing: 8

            Item {
                width: parent.width
                height: 22
                Text {
                    anchors.left: parent.left
                    anchors.right: buttons.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.sound.name || "") + (root.sound.seconds > 0 ? "  ·  " + Number(root.sound.seconds).toFixed(2) + " s" : "")
                    color: theme.text
                    font.pixelSize: theme.fontSize
                    elide: Text.ElideMiddle
                }
                Row {
                    id: buttons
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    VButton { text: "▶"; quiet: true; tip: "Listen to the sound on its own"; onClicked: app.previewLayerSound() }
                    VButton { text: "Change"; quiet: true; tip: "Choose another sound from the library"; onClicked: library.open() }
                    VButton { text: "File…"; quiet: true; tip: "Use a sound file of your own"; onClicked: soundDialog.open() }
                    VButton { text: "✕"; quiet: true; tip: "Remove the sound from this layer"; onClicked: app.clearSound() }
                }
            }

            // The waveform.
            Rectangle {
                width: parent.width
                height: 46
                radius: 4
                color: theme.field
                border.width: 1
                border.color: theme.line
                Canvas {
                    id: wave
                    anchors.fill: parent
                    anchors.margins: 3
                    property var peaks: root.sound.peaks || []
                    onPeaksChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var n = peaks.length / 2
                        if (n < 1)
                            return
                        ctx.fillStyle = root.mute ? "#646a77" : "#ff9a3c"
                        var mid = height / 2
                        for (var i = 0; i < n; ++i) {
                            var x = i * width / n
                            var lo = peaks[2 * i], hi = peaks[2 * i + 1]
                            var top = mid - hi * mid, bottom = mid - lo * mid
                            ctx.fillRect(x, top, Math.max(1, width / n - 0.5), Math.max(1, bottom - top))
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: !!root.sound.problem
                    text: root.sound.problem || ""
                    color: theme.faint
                    font.pixelSize: theme.smallFontSize
                }
            }

            // When it plays.
            Flow {
                width: parent.width
                spacing: 6
                VButton {
                    text: "When the layer starts"
                    checked: root.play === "start"
                    onClicked: { root.play = "start"; app.setSoundChoice("play", "start") }
                }
                VButton {
                    text: "Every burst"
                    checked: root.play === "bursts"
                    tip: "Plays at each burst of this layer's particles. A layer with no bursts plays nothing."
                    onClicked: { root.play = "bursts"; app.setSoundChoice("play", "bursts") }
                }
            }

            Repeater {
                model: {
                    var basic = [
                        { key: "delay", label: "Delay", unit: "seconds (below 0 plays early)", from: -0.5, to: 1, decimals: 2 },
                        { key: "volume", label: "Volume", unit: "", from: 0, to: 2, decimals: 2 },
                        { key: "pitch", label: "Pitch", unit: "semitones", from: -12, to: 12, decimals: 1 },
                        { key: "pan", label: "Pan", unit: "left ↔ right", from: -1, to: 1, decimals: 2 },
                        { key: "randomPitch", label: "Random pitch", unit: "semitones, so repeats vary", from: 0, to: 4, decimals: 1 }
                    ]
                    var more = [
                        { key: "randomVolume", label: "Random volume", unit: "", from: 0, to: 1, decimals: 2 },
                        { key: "fadeIn", label: "Fade in", unit: "seconds", from: 0, to: 2, decimals: 2 },
                        { key: "fadeOut", label: "Fade out", unit: "seconds", from: 0, to: 2, decimals: 2 },
                        { key: "trimStart", label: "Trim start", unit: "seconds skipped", from: 0, to: Math.max(0.5, root.sound.seconds || 1), decimals: 2 },
                        { key: "length", label: "Length", unit: "seconds (0 plays it all)", from: 0, to: Math.max(0.5, root.sound.seconds || 1), decimals: 2 }
                    ]
                    return root.showMore ? basic.concat(more) : basic
                }
                delegate: Column {
                    id: slot
                    required property var modelData
                    width: column.width
                    spacing: 2
                    property real value: root.sound[modelData.key] !== undefined ? root.sound[modelData.key] : 0
                    function apply(v) {
                        slot.value = Number(v.toFixed(slot.modelData.decimals + 1))
                        app.setSoundNumber(slot.modelData.key, slot.value)
                    }
                    ControlHeader {
                        width: parent.width
                        label: slot.modelData.label
                        unit: slot.modelData.unit
                    }
                    Item {
                        width: parent.width
                        height: 24
                        VSlider {
                            anchors.left: parent.left
                            anchors.right: box.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            from: slot.modelData.from
                            to: slot.modelData.to
                            value: slot.value
                            onDragStarted: app.beginEdit("Change " + slot.modelData.label)
                            onDragEnded: app.endEdit()
                            onMoved: function(v) { slot.apply(v) }
                        }
                        VNumberField {
                            id: box
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            value: slot.value
                            decimals: slot.modelData.decimals
                            onEdited: function(v) { slot.apply(v) }
                        }
                    }
                }
            }

            Flow {
                width: parent.width
                spacing: 12
                VCheck {
                    text: "Loop until the layer ends"
                    checked: root.loop
                    onToggled: function(on) { root.loop = on; app.setSoundFlag("loop", on) }
                }
                VCheck {
                    text: "Mute"
                    checked: root.mute
                    onToggled: function(on) { root.mute = on; app.setSoundFlag("mute", on); wave.requestPaint() }
                }
            }
            VButton {
                text: root.showMore ? "Fewer settings" : "More settings"
                quiet: true
                onClicked: root.showMore = !root.showMore
            }
        }
    }
}
