import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 560
    height: 780
    minimumWidth: 520
    minimumHeight: 620
    visible: true
    title: qsTr("Clevo Control")
    color: theme.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    palette.window: theme.background
    palette.windowText: theme.foreground
    palette.text: theme.foreground

    property bool showAdvanced: false

    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: helpPopup.open() }
    Shortcut {
        sequence: "Ctrl+1"
        context: Qt.ApplicationShortcut
        onActivated: tabs.currentIndex = 0
    }
    Shortcut {
        sequence: "Ctrl+2"
        context: Qt.ApplicationShortcut
        onActivated: tabs.currentIndex = 1
    }

    header: Rectangle {
        height: 104
        color: theme.background

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 10
                spacing: 12

                Rectangle {
                    width: 28; height: 28
                    radius: Math.min(4, theme.radius)
                    color: backend.color
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.25)
                }
                Label {
                    text: qsTr("Clevo Control")
                    font.pixelSize: 17
                    font.bold: true
                    color: theme.foreground
                    Layout.fillWidth: true
                }
                Label {
                    text: tabs.currentIndex === 0 ? backend.status : fan.fanStatus
                    font.pixelSize: 12
                    color: Qt.rgba(1, 1, 1, 0.55)
                    elide: Text.ElideRight
                    Layout.maximumWidth: 240
                }
            }

            TabBar {
                id: tabs
                objectName: "tabs"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 6
                background: Item {}

                TabButton {
                    text: qsTr("Keyboard")
                    font.pixelSize: 13
                }
                TabButton {
                    text: qsTr("Fan")
                    font.pixelSize: 13
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }
        }
    }

    StackLayout {
        id: pages
        anchors.fill: parent
        anchors.margins: 20
        currentIndex: tabs.currentIndex

        // ================= Keyboard =================
        ScrollView {
            id: kbScroll
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: kbScroll.availableWidth
                spacing: 18

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

                Label {
                    text: qsTr("Colour")
                    font.pixelSize: 13
                    font.bold: true
                    color: Qt.rgba(1, 1, 1, 0.75)
                }

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

                ColorPicker {
                    id: picker
                    objectName: "picker"
                    Layout.fillWidth: true
                    color: backend.color
                    onPicked: (c) => backend.color = c
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
                    Button {
                        text: qsTr("Turn off")
                        onClicked: backend.turnOff()
                    }
                }

                // ---- Idle auto-off ----
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Turn off when idle")
                            font.pixelSize: 13
                            font.bold: true
                            color: Qt.rgba(1, 1, 1, 0.75)
                        }
                        Label {
                            text: idle.deviceAvailable
                                  ? qsTr("The light comes back the moment you type.")
                                  : qsTr("Keyboard device not readable — see the note below.")
                            font.pixelSize: 11
                            color: idle.deviceAvailable ? Qt.rgba(1, 1, 1, 0.4) : "#FF9F0A"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    Switch {
                        checked: idle.enabled
                        enabled: idle.deviceAvailable
                        onToggled: idle.enabled = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: idle.enabled
                    Label {
                        text: qsTr("After")
                        font.pixelSize: 12
                        color: Qt.rgba(1, 1, 1, 0.6)
                    }
                    Slider {
                        id: idleSlider
                        Layout.fillWidth: true
                        from: 10
                        to: 300
                        stepSize: 10
                        snapMode: Slider.SnapAlways
                        value: idle.timeoutSeconds
                        onMoved: idle.timeoutSeconds = Math.round(value)
                    }
                    Label {
                        text: qsTr("%n s", "", idle.timeoutSeconds)
                        font.pixelSize: 12
                        color: theme.accent
                        Layout.preferredWidth: 52
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: idle.enabled && idle.dimmed
                    text: qsTr("Backlight is off — type to bring it back.")
                    font.pixelSize: 11
                    color: Qt.rgba(1, 1, 1, 0.45)
                }

                Label {
                    Layout.fillWidth: true
                    visible: !idle.deviceAvailable
                    text: qsTr("Reading key presses needs membership of the input group:\n  sudo usermod -aG input $USER\nthen log out and back in.")
                    font.pixelSize: 11
                    color: Qt.rgba(1, 1, 1, 0.45)
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: root.showAdvanced ? qsTr("Hide diagnostics") : qsTr("Diagnostics")
                        flat: true
                        onClicked: root.showAdvanced = !root.showAdvanced
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: root.showAdvanced
                    implicitHeight: 150
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
                            text: qsTr("This model maps the payload bytes as A=blue, B=red, C=green (BRG). The picker already compensates.")
                            font.pixelSize: 11
                            color: Qt.rgba(1, 1, 1, 0.45)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            spacing: 8
                            Button { text: qsTr("A → blue");  onClicked: backend.applyRaw("0x0FFF0000") }
                            Button { text: qsTr("B → red");   onClicked: backend.applyRaw("0x0F00FF00") }
                            Button { text: qsTr("C → green"); onClicked: backend.applyRaw("0x0F0000FF") }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ================= Fan =================
        ScrollView {
            id: fanScroll
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: fanScroll.availableWidth
                spacing: 18

                // Telemetry
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Repeater {
                        model: [
                            { t: qsTr("CPU"), temp: fan.cpuTemp, rpm: fan.cpuRpm },
                            { t: qsTr("GPU"), temp: fan.gpuTemp, rpm: fan.gpuRpm }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 92
                            radius: Math.min(4, theme.radius)
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 4
                                Label {
                                    text: modelData.t
                                    font.pixelSize: 12
                                    color: Qt.rgba(1, 1, 1, 0.5)
                                }
                                Label {
                                    text: modelData.temp > 0
                                          ? modelData.temp.toLocaleString(Qt.locale(), 'f', 0) + " °C"
                                          : "—"
                                    font.pixelSize: 26
                                    font.bold: true
                                    color: modelData.temp >= 80 ? "#FF453A"
                                         : modelData.temp >= 65 ? "#FFD60A"
                                         : theme.foreground
                                }
                                Label {
                                    text: modelData.rpm.toLocaleString(Qt.locale(), 'f', 0) + " RPM"
                                    font.pixelSize: 12
                                    color: Qt.rgba(1, 1, 1, 0.55)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                Label {
                    text: qsTr("Fan speed")
                    font.pixelSize: 13
                    font.bold: true
                    color: Qt.rgba(1, 1, 1, 0.75)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Button {
                        text: qsTr("Automatic")
                        highlighted: fan.autoMode
                        enabled: fan.connected
                        onClicked: fan.setAuto()
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: fan.connected
                              ? (fan.autoMode ? qsTr("firmware curve")
                                              : fan.duty.toLocaleString(Qt.locale(), 'f', 0) + "%")
                              : qsTr("daemon offline")
                        font.pixelSize: 14
                        font.bold: true
                        color: fan.connected ? theme.accent : Qt.rgba(1, 1, 1, 0.35)
                    }
                }

                // Manual duty steps, matching what the EC accepts (40-100).
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 8
                    rowSpacing: 8

                    Repeater {
                        model: [40, 50, 60, 70, 80, 90, 100]
                        delegate: Button {
                            required property int modelData
                            Layout.fillWidth: true
                            text: modelData.toLocaleString(Qt.locale(), 'f', 0) + "%"
                            flat: !(fan.duty === modelData && !fan.autoMode)
                            highlighted: fan.duty === modelData && !fan.autoMode
                            enabled: fan.connected
                            onClicked: fan.setDuty(modelData)
                        }
                    }
                }

                Label {
                    text: qsTr("Manual duty below 40% stalls the fans, so the EC refuses it.")
                    font.pixelSize: 11
                    color: Qt.rgba(1, 1, 1, 0.4)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                Label {
                    text: qsTr("Status")
                    font.pixelSize: 13
                    font.bold: true
                    color: Qt.rgba(1, 1, 1, 0.75)
                }
                Label {
                    text: fan.connected
                          ? qsTr("Connected to the fan daemon.")
                          : qsTr("The fan daemon (clevo-indicator-dual) is not running. Start it with:\n  systemctl --user start clevo-indicator-dual")
                    font.pixelSize: 12
                    color: Qt.rgba(1, 1, 1, 0.5)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    Popup {
        id: helpPopup
        anchors.centerIn: parent
        width: 340
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
                    { k: "Ctrl+1", d: qsTr("Keyboard tab") },
                    { k: "Ctrl+2", d: qsTr("Fan tab") },
                    { k: "Q",      d: qsTr("Quit") },
                    { k: "?",      d: qsTr("Show this help") }
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
