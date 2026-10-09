import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Getting an effect out of VFX Forge: into Unity, or as animation frames.
Rectangle {
    id: root

    // The viewport's camera, so frames show what the window shows.
    property var viewState: ({})
    signal closeRequested()

    property int frameSize: 512
    property bool seeThrough: true
    property bool withSheet: true

    color: "#c8000000"

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
        onClicked: root.closeRequested()
    }

    FolderDialog {
        id: unityFolderDialog
        title: "Choose your Unity project's folder"
        onAccepted: if (app.exportToUnity(selectedFolder)) root.closeRequested()
    }
    FileDialog {
        id: packageDialog
        title: "Save a Unity package"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "unitypackage"
        nameFilters: ["Unity packages (*.unitypackage)"]
        onAccepted: if (app.exportUnityPackage(selectedFile)) root.closeRequested()
    }
    FolderDialog {
        id: framesFolderDialog
        title: "Choose where to put the frames"
        onAccepted: {
            if (app.exportFrames(selectedFolder, root.frameSize, root.seeThrough, root.withSheet, root.viewState))
                root.closeRequested()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 760)
        height: content.implicitHeight + 48
        radius: 10
        color: theme.panel
        border.color: theme.line

        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Export “" + app.effectName + "”"
                    color: theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                VButton { text: "Close"; onClicked: root.closeRequested() }
            }

            // ------------------------------------------------------ Unity
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: unityColumn.implicitHeight + 28
                radius: 8
                color: theme.raised
                border.color: theme.line

                ColumnLayout {
                    id: unityColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    Text {
                        text: "Unity particle effect"
                        color: theme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.dim
                        font.pixelSize: theme.fontSize
                        text: "A prefab made of Unity's own Particle System, one per layer, drawn with VFX Forge's shapes. "
                              + "It appears in Assets/VFXForge/Effects; drag it into a scene. Export again to update it everywhere it is used."
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        VButton {
                            visible: app.lastUnityProject.length > 0
                            primary: true
                            text: "Export to “" + app.lastUnityProjectName + "”"
                            tip: app.lastUnityProject
                            onClicked: if (app.exportToUnityPath(app.lastUnityProject)) root.closeRequested()
                        }
                        VButton {
                            primary: app.lastUnityProject.length === 0
                            text: app.lastUnityProject.length > 0 ? "Another Unity project…" : "Export to a Unity project…"
                            tip: "Choose the project's folder: the one with Assets and ProjectSettings in it"
                            onClicked: unityFolderDialog.open()
                        }
                        VButton {
                            text: "Save as .unitypackage…"
                            tip: "One file to send to someone, for Assets > Import Package"
                            onClicked: packageDialog.open()
                        }
                    }
                }
            }

            // ------------------------------------------------------ frames
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: framesColumn.implicitHeight + 28
                radius: 8
                color: theme.raised
                border.color: theme.line

                ColumnLayout {
                    id: framesColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    Text {
                        text: "Animation frames (PNG)"
                        color: theme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.dim
                        font.pixelSize: theme.fontSize
                        text: "Exactly what the viewport shows, one picture per frame: " + app.exportFrameCount
                              + " frames at " + app.frameRate.toFixed(0) + " frames per second. "
                              + "In Unity, select all the frames and drag them into a scene to make the animation."
                    }
                    RowLayout {
                        spacing: 6
                        Text { text: "Size"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.preferredWidth: 80 }
                        Repeater {
                            model: [256, 512, 1024]
                            delegate: VButton {
                                required property int modelData
                                text: modelData + " px"
                                checked: root.frameSize === modelData
                                onClicked: root.frameSize = modelData
                            }
                        }
                    }
                    RowLayout {
                        spacing: 6
                        Text { text: "Background"; color: theme.dim; font.pixelSize: theme.fontSize; Layout.preferredWidth: 80 }
                        VButton { text: "See-through"; checked: root.seeThrough; onClicked: root.seeThrough = true }
                        VButton { text: "Black"; checked: !root.seeThrough; tip: "For glowing effects drawn with additive blending"; onClicked: root.seeThrough = false }
                    }
                    VCheck {
                        text: "Also make one sprite sheet with every frame"
                        checked: root.withSheet
                        onToggled: function(on) { root.withSheet = on }
                    }
                    VButton {
                        primary: true
                        text: "Export frames…"
                        onClicked: framesFolderDialog.open()
                    }
                }
            }
        }
    }
}
