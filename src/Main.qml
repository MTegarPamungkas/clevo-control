import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 560
    height: 760
    visible: true
    title: qsTr("Keyboard Light")
    color: theme.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    // The accent is what the user reads as "the app", so every control that
    // can carry it does.
    palette.window: theme.background
    palette.windowText: theme.foreground
    palette.text: theme.foreground

    property color preview: backend.color
    property bool showAdvanced: false

    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: helpPopup.open() }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: backend.apply()
    }

    header: Rectangle {
        height: 56
        color: theme.background
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 12

            Rectangle {
                width: 28; height: 28
                radius: Math.min(4, theme.radius)
                color: root.preview
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.25)
            }
            Label {
                text: qsTr("Keyboard Light")
                font.pixelSize: 17
                font.bold: true
                color: theme.foreground
                Layout.fillWidth: true
            }
            Label {
                text: backend.status
                font.pixelSize: 12
                color: Qt.rgba(1, 1, 1, 0.55)
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
        }
    }

    minimumWidth: 520
    minimumHeight: 640

    ScrollView {
        id: scroller
        anchors.fill: parent
        anchors.margins: 20
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

    ColumnLayout {
        width: scroller.availableWidth
        spacing: 18

        // ---- Brightness ----
        Label {
            text: qsTr("Brightness")
            font.pixelSize: 13
            font.bold: true
            color: Qt.rgba(1, 1, 1, 0.75)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Slider {
                id: bright
                Layout.fillWidth: true
                from: 0
                to: 9
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: backend.brightness
                onMoved: backend.brightness = Math.round(value)
            }

            Label {
                text: backend.brightness === 0
                      ? qsTr("off")
                      : backend.brightness.toLocaleString(Qt.locale(), 'f', 0)
                font.pixelSize: 14
                font.bold: true
                color: theme.accent
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 42
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: 9
                delegate: Button {
                    required property int index
                    Layout.fillWidth: true
                    text: (index + 1).toLocaleString(Qt.locale(), 'f', 0)
                    flat: true
                    highlighted: backend.brightness === index + 1
                    onClicked: {
                        bright.value = index + 1
                        backend.brightness = index + 1
                    }
                }
            }
        }

        // ---- Colour ----
        Label {
            text: qsTr("Colour")
            font.pixelSize: 13
            font.bold: true
            color: Qt.rgba(1, 1, 1, 0.75)
        }

        // Quick presets stay: they are one click and cover the common picks.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: [
                    { n: qsTr("Red"),     c: "#FF3B30" },
                    { n: qsTr("Orange"),  c: "#FF9500" },
                    { n: qsTr("Yellow"),  c: "#FFD60A" },
                    { n: qsTr("Green"),   c: "#34C759" },
                    { n: qsTr("Cyan"),    c: "#32D4E0" },
                    { n: qsTr("Blue"),    c: "#0A84FF" },
                    { n: qsTr("Purple"),  c: "#BF5AF2" },
                    { n: qsTr("White"),   c: "#FFFFFF" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: Math.min(4, theme.radius)
                    color: modelData.c
                    border.width: backend.color.toString().toUpperCase() === modelData.c.toUpperCase() ? 3 : 1
                    border.color: backend.color.toString().toUpperCase() === modelData.c.toUpperCase()
                                  ? theme.accent : Qt.rgba(1, 1, 1, 0.2)
                    MouseArea {
                        anchors.fill: parent
                        onClicked: backend.color = modelData.c
                    }
                    ToolTip.visible: hover.hovered
                    ToolTip.text: modelData.n
                    HoverHandler { id: hover }
                }
            }
        }

        // The picker previews live; the hardware is only written on release so
        // dragging does not flood the EC with dozens of ACPI calls.
        ColorPicker {
            id: picker
            objectName: "picker"
            Layout.fillWidth: true
            color: backend.color
            onPicked: (c) => {
                backend.color = c
                backend.apply()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                width: 40; height: 28
                radius: Math.min(4, theme.radius)
                color: backend.color
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.25)
            }
            Label {
                text: backend.color.toString().toUpperCase()
                font.family: "monospace"
                font.pixelSize: 13
                color: Qt.rgba(1, 1, 1, 0.7)
                Layout.fillWidth: true
            }
        }

        // ---- Actions ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Button {
                text: qsTr("Apply")
                highlighted: true
                onClicked: backend.apply()
            }
            Button {
                text: qsTr("Turn off")
                onClicked: backend.turnOff()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: root.showAdvanced ? qsTr("Hide diagnostics") : qsTr("Diagnostics")
                flat: true
                onClicked: root.showAdvanced = !root.showAdvanced
            }
        }

        // ---- Diagnostics ----
        // The channel order this model forwards to the EC is not the same as
        // the RGB order a colour picker assumes, so expose the raw channels
        // for the user to identify what actually drives each colour.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showAdvanced
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(1, 1, 1, 0.04)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                Label {
                    text: qsTr("Test one channel at a time")
                    font.pixelSize: 12
                    font.bold: true
                    color: Qt.rgba(1, 1, 1, 0.7)
                }
                Label {
                    text: qsTr("This model maps the payload bytes as A=blue, B=red, C=green (BRG, not RGB). The colour picker already compensates; these buttons send each payload byte alone.")
                    font.pixelSize: 11
                    color: Qt.rgba(1, 1, 1, 0.45)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    spacing: 8
                    Button {
                        text: qsTr("A → blue")
                        onClicked: backend.applyRaw("0x0FFF0000")
                    }
                    Button {
                        text: qsTr("B → red")
                        onClicked: backend.applyRaw("0x0F00FF00")
                    }
                    Button {
                        text: qsTr("C → green")
                        onClicked: backend.applyRaw("0x0F0000FF")
                    }
                }
                Label {
                    text: qsTr("Device: %1").arg(backend.available ? qsTr("helper ready") : qsTr("helper missing"))
                    font.pixelSize: 11
                    color: Qt.rgba(1, 1, 1, 0.45)
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.fillHeight: !root.showAdvanced }
    }
    }

    Popup {
        id: helpPopup
        anchors.centerIn: parent
        width: 320
        modal: true
        padding: 18

        background: Rectangle {
            color: Qt.lighter(theme.background, 1.6)
            radius: Math.min(6, theme.radius)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 15
                font.bold: true
                color: theme.foreground
            }
            Repeater {
                model: [
                    { k: "Space", d: qsTr("Apply brightness and colour") },
                    { k: "Q",     d: qsTr("Quit") },
                    { k: "?",     d: qsTr("Show this help") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        text: modelData.k
                        font.family: "monospace"
                        font.pixelSize: 12
                        color: theme.accent
                        Layout.preferredWidth: 70
                    }
                    Label {
                        text: modelData.d
                        font.pixelSize: 12
                        color: theme.foreground
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Button {
                text: qsTr("Close")
                Layout.alignment: Qt.AlignRight
                onClicked: helpPopup.close()
            }
        }
    }
}
