import QtQuick 2.0
import Sailfish.Silica 1.0

// Two rows of keys the Sailfish keyboard lacks: Esc, Ctrl, Alt, Tab, arrows (as an
// inverted T under pgup / ↑ / pgdn), paging, and symbols that are awkward to reach.
// Ctrl and Alt are one-shot: they apply to the next key, typed or tapped here.
// Keys never take focus, so the keyboard stays open while you use them.
Rectangle {
    id: bar

    property QtObject terminal
    signal hideKeyboard()

    readonly property real keyHeight: Math.round(Theme.itemSizeExtraSmall * 0.8)
    readonly property real spacing: Math.round(Theme.paddingSmall * 0.75)

    readonly property var rows: [
        [{ k: "esc" }, { k: "tab" }, { t: "~" }, { t: "|" }, { t: "/" }, { k: "home" }, { k: "pgup" }, { k: "up", r: true }, { k: "pgdn" }],
        [{ m: "ctrl" }, { m: "alt" }, { a: "hide" }, { t: "-" }, { t: "_" }, { k: "end" }, { k: "left", r: true }, { k: "down", r: true }, { k: "right", r: true }]
    ]

    function labelFor(key) {
        if (key.t) return key.t
        if (key.m) return key.m
        if (key.k === "pgup" || key.k === "pgdn" || key.k === "home" || key.k === "end" || key.k === "esc" || key.k === "tab")
            return key.k
        return ""
    }

    function iconFor(key) {
        switch (key.k || key.a) {
        case "up": return "image://theme/icon-m-up"
        case "down": return "image://theme/icon-m-down"
        case "left": return "image://theme/icon-m-left"
        case "right": return "image://theme/icon-m-right"
        case "hide": return "image://theme/icon-m-keyboard"
        }
        return ""
    }

    function press(key) {
        if (!terminal)
            return
        if (key.a === "hide")
            hideKeyboard()
        else if (key.m === "ctrl")
            terminal.ctrlModifier = !terminal.ctrlModifier
        else if (key.m === "alt")
            terminal.altModifier = !terminal.altModifier
        else if (key.t)
            terminal.sendText(key.t)
        else
            terminal.sendKey(key.k)
    }

    height: 2 * keyHeight + 3 * spacing
    color: "#161920"

    Rectangle {
        width: parent.width
        height: 1
        color: Qt.rgba(1, 1, 1, 0.08)
    }

    Column {
        anchors {
            fill: parent
            margins: bar.spacing
        }
        spacing: bar.spacing

        Repeater {
            model: bar.rows

            Row {
                property var keys: modelData
                spacing: bar.spacing

                Repeater {
                    model: keys

                    Rectangle {
                        id: keyRect

                        readonly property var key: modelData
                        readonly property bool active: (key.m === "ctrl" && terminal && terminal.ctrlModifier)
                                                       || (key.m === "alt" && terminal && terminal.altModifier)

                        width: (bar.width - 2 * bar.spacing - 8 * bar.spacing) / 9
                        height: bar.keyHeight
                        radius: Theme.paddingSmall
                        color: active ? "#4AC9A3" : (mouse.pressed ? Qt.rgba(0.29, 0.79, 0.64, 0.5) : "#2A2E38")

                        Label {
                            anchors.centerIn: parent
                            visible: text !== ""
                            text: bar.labelFor(key)
                            color: keyRect.active ? "#0D0F14" : "#DADFE8"
                            font.family: "DejaVu Sans Mono"
                            font.pixelSize: text.length === 1 ? Theme.fontSizeMedium : Theme.fontSizeExtraSmall
                        }

                        Image {
                            anchors.centerIn: parent
                            visible: source != ""
                            source: bar.iconFor(key) !== "" ? bar.iconFor(key) + "?#DADFE8" : ""
                            sourceSize.width: Theme.iconSizeSmallPlus
                            sourceSize.height: Theme.iconSizeSmallPlus
                        }

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            onPressed: {
                                bar.press(key)
                                if (key.r) {
                                    repeatTimer.interval = 400
                                    repeatTimer.restart()
                                }
                            }
                            onReleased: repeatTimer.stop()
                            onCanceled: repeatTimer.stop()
                        }

                        // Arrow keys repeat while held: after 400 ms, then every 60 ms.
                        Timer {
                            id: repeatTimer
                            interval: 400
                            repeat: true
                            onTriggered: {
                                interval = 60
                                bar.press(key)
                            }
                        }
                    }
                }
            }
        }
    }
}
