import QtQuick
import QtQuick.Controls.Basic

// How heavy the effect is to draw on a phone, always in view. Click for what
// makes it heavy, which layers cost most, and a one-click lighter version.
Rectangle {
    id: badge
    objectName: "budgetBadge"

    readonly property var b: app.budget
    readonly property int grade: b && b.grade !== undefined ? b.grade : 0
    readonly property var gradeColors: ["#3ecf8e", "#c9d84a", "#f0a33a", "#f05a4a"]
    function gradeColor(g) { return gradeColors[Math.max(0, Math.min(3, g))] }

    implicitWidth: label.implicitWidth + 30
    implicitHeight: 28
    radius: 14
    color: area.containsMouse ? theme.hover : theme.raised
    border.width: 1
    border.color: gradeColor(grade)

    Rectangle {
        id: dot
        width: 8; height: 8; radius: 4
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        color: badge.gradeColor(badge.grade)
    }
    Text {
        id: label
        anchors.left: dot.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: "Phone: " + (badge.b && badge.b.gradeName ? badge.b.gradeName : "…")
        color: theme.text
        font.pixelSize: theme.fontSize
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: panel.open()
    }
    ToolTip.visible: area.containsMouse && !panel.visible
    ToolTip.delay: 600
    ToolTip.text: "How heavy this effect is to draw on a low-end phone. Click for details."

    Popup {
        id: panel
        objectName: "budgetPanel"
        y: badge.height + 8
        x: Math.min(0, (badge.parent ? badge.parent.width : 2000) - badge.x - width - 8)
        width: 400
        padding: 14
        modal: true
        background: Rectangle { color: theme.panel; border.color: theme.line; radius: 8 }

        contentItem: Column {
            spacing: 10

            Row {
                spacing: 8
                Rectangle {
                    width: 12; height: 12; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: badge.gradeColor(badge.grade)
                }
                Text {
                    text: (badge.b.gradeName || "") + " for a low-end phone"
                    color: theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
            }

            // The three measures, each with its own grade.
            Grid {
                columns: 3
                columnSpacing: 10
                rowSpacing: 6
                Repeater {
                    model: [
                        { name: "Particles at once", value: String(badge.b.particles || 0), grade: badge.b.particlesGrade || 0,
                          help: "under 400 is comfortable" },
                        { name: "Screen painted", value: (badge.b.fill || 0).toFixed(1) + "×", grade: badge.b.fillGrade || 0,
                          help: "overdraw at its busiest; under 4× is comfortable" },
                        { name: "Separate draws", value: String(badge.b.draws || 0), grade: badge.b.drawsGrade || 0,
                          help: "one per layer, one more per trail" }
                    ]
                    delegate: Column {
                        required property var modelData
                        width: 118
                        spacing: 2
                        Text { text: modelData.name; color: theme.dim; font.pixelSize: theme.smallFontSize }
                        Row {
                            spacing: 6
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: badge.gradeColor(modelData.grade)
                            }
                            Text { text: modelData.value; color: theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                        }
                        Text {
                            width: parent.width
                            text: modelData.help
                            color: theme.faint
                            font.pixelSize: theme.smallFontSize - 1
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Text {
                visible: (badge.b.lights || 0) > 0
                text: "Real lights in Unity: up to " + (badge.b.lights || 0)
                color: theme.dim
                font.pixelSize: theme.smallFontSize
            }

            // Which layers cost most: a bar for each one's share of the busiest moment.
            Text { text: "What costs most"; color: theme.dim; font.pixelSize: theme.smallFontSize }
            Repeater {
                model: (badge.b.layers || []).slice(0, 5)
                delegate: Item {
                    required property var modelData
                    width: 372
                    height: 18
                    Text {
                        id: layerName
                        width: 120
                        text: modelData.name
                        elide: Text.ElideRight
                        color: theme.text
                        font.pixelSize: theme.smallFontSize
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        id: track
                        anchors.left: layerName.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 150; height: 6; radius: 3
                        color: theme.field
                        Rectangle {
                            width: Math.max(2, parent.width * Math.min(1, modelData.share))
                            height: parent.height; radius: 3
                            color: badge.gradeColor(badge.b.fillGrade || 0)
                        }
                    }
                    Text {
                        anchors.left: track.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.particles + " particles" + (modelData.trail ? ", trail" : "")
                        color: theme.dim
                        font.pixelSize: theme.smallFontSize
                    }
                }
            }

            Repeater {
                model: badge.b.notes || []
                delegate: Text {
                    required property string modelData
                    width: 372
                    text: "• " + modelData
                    color: theme.text
                    font.pixelSize: theme.smallFontSize
                    wrapMode: Text.WordWrap
                }
            }

            Row {
                spacing: 10
                VButton {
                    primary: true
                    text: "Make mobile-safe"
                    enabled: badge.grade >= 2
                    tip: "Fewer or smaller particles in the heaviest layers, until it is OK on a phone. One step to undo."
                    onClicked: { app.makeMobileSafe(); }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 230
                    text: badge.grade >= 2 ? "One step to undo; locked layers are left alone."
                                           : "Nothing to trim: it is fine for phones."
                    color: theme.faint
                    font.pixelSize: theme.smallFontSize
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                width: 372
                text: "Measured with the effect on a portrait phone screen 10 units tall (a Unity orthographic camera of size 5). " +
                      "Rules of thumb for low-end phones; bigger phones can afford more."
                color: theme.faint
                font.pixelSize: theme.smallFontSize - 1
                wrapMode: Text.WordWrap
            }
        }
    }
}
