import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// HSV colour picker: a saturation/value square plus a hue strip.
//
// The square is two gradients rather than a shader: white-to-hue
// horizontally, then transparent-to-black vertically. That is cheap, works on
// any backend, and is accurate enough for a keyboard backlight.
//
// The picker owns hue/sat/val as real numbers because converting a QColor
// back to HSV loses the hue of greys and blacks (hsvHue is -1 there), which
// would make the marker jump to the corner while dragging to white or black.
Item {
    id: picker

    property color color: "#FFFFFF"
    property real hue: 0.0
    property real sat: 1.0
    property real val: 1.0

    // While the user drags, the picker is the source of truth. Without this,
    // the `color` binding fed back from the backend re-derived hue/sat/val and
    // overwrote the drag, which is what made the marker jump back and the
    // hardware appear stuck on the old colour.
    property bool dragging: false

    signal picked(color c)

    implicitHeight: 176

    onColorChanged: {
        if (!dragging)
            syncFromColor()
    }

    function syncFromColor() {
        // A QML `color` is a value type, not an object: `color.isValid()` does
        // not exist and throws a TypeError, which aborted this function before
        // it could copy the hue across. That is why dragging the hue strip left
        // the colour stuck on red.
        if (color.a === undefined)
            return
        var h = color.hsvHue
        // Grey and black report -1; keep the previous hue so the marker does
        // not snap to red while the user drags into the corner.
        if (h >= 0)
            picker.hue = h
        picker.sat = color.hsvSaturation
        picker.val = color.hsvValue
    }

    function currentColor() {
        // Qt.hsva() takes hue in 0.0-1.0, NOT 0-360. Passing 240 wrapped
        // around to 0 and produced red, which is why the hue strip appeared
        // dead and the light stayed red no matter where it was dragged.
        return Qt.hsva(picker.hue / 360.0, picker.sat, picker.val, 1.0)
    }

    function emitColor() {
        picker.picked(currentColor())
    }

    Column {
        anchors.fill: parent
        spacing: 10

        // ---- Saturation / value square ----
        Rectangle {
            id: svSquare
            width: parent.width
            height: parent.height - 26
            radius: Math.min(4, theme.radius)
            clip: true
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.18)

            // Saturation: white on the left, full hue on the right.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#FFFFFF" }
                    GradientStop { position: 1.0; color: Qt.hsva(picker.hue / 360.0, 1.0, 1.0, 1.0) }
                }
            }

            // Value: transparent at the top, black at the bottom.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: "#000000" }
                }
            }

            // Marker
            Rectangle {
                width: 16; height: 16
                radius: 8
                x: picker.sat * (svSquare.width - width)
                y: (1.0 - picker.val) * (svSquare.height - height)
                color: "transparent"
                border.width: 2.5
                border.color: picker.val > 0.6 && picker.sat < 0.4 ? "#000000" : "#FFFFFF"
                antialiasing: true
            }

            MouseArea {
                anchors.fill: parent
                function apply(x, y) {
                    picker.sat = Math.max(0, Math.min(1, x / width))
                    picker.val = 1.0 - Math.max(0, Math.min(1, y / height))
                    picker.emitColor()
                }
                onPressed: (m) => { picker.dragging = true; apply(m.x, m.y) }
                onPositionChanged: (m) => { if (pressed) apply(m.x, m.y) }
                onReleased: picker.dragging = false
                onCanceled: picker.dragging = false
            }
        }

        // ---- Hue strip ----
        Rectangle {
            id: hueStrip
            width: parent.width
            height: 16
            radius: Math.min(4, theme.radius)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.18)
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.000; color: "#FF0000" }
                    GradientStop { position: 0.167; color: "#FFFF00" }
                    GradientStop { position: 0.333; color: "#00FF00" }
                    GradientStop { position: 0.500; color: "#00FFFF" }
                    GradientStop { position: 0.667; color: "#0000FF" }
                    GradientStop { position: 0.833; color: "#FF00FF" }
                    GradientStop { position: 1.000; color: "#FF0000" }
                }
            }

            // Marker
            Rectangle {
                width: 6; height: parent.height
                x: (picker.hue / 360.0) * (hueStrip.width - width)
                radius: Math.min(3, theme.radius)
                color: "transparent"
                border.width: 2.5
                border.color: "#FFFFFF"
                antialiasing: true
            }

            MouseArea {
                anchors.fill: parent
                function apply(x) {
                    picker.hue = Math.max(0, Math.min(1, x / width)) * 360.0
                    picker.emitColor()
                }
                onPressed: (m) => { picker.dragging = true; apply(m.x) }
                onPositionChanged: (m) => { if (pressed) apply(m.x) }
                onReleased: picker.dragging = false
                onCanceled: picker.dragging = false
            }
        }
    }
}
