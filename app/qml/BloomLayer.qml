import QtQuick
import QtQuick.Effects

// Light bleeding from bright parts of the effect: a wide and a tight blurred
// copy of the viewport, laid over it. The viewport's pixels are premultiplied,
// so a copy laid over it adds its light (additive particles have no alpha).
// Loaded on its own, so if the effects module is missing the app still runs.
Item {
    id: root
    property Item source: null
    property real amount: 0.8
    anchors.fill: parent

    MultiEffect {
        anchors.fill: parent
        source: root.source
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: 64
        blur: 1.0
        opacity: 0.85 * root.amount
    }
    MultiEffect {
        anchors.fill: parent
        source: root.source
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: 24
        blur: 0.55
        opacity: 0.7 * root.amount
    }
}
