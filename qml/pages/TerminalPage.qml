import QtQuick 2.0
import Sailfish.Silica 1.0
import PocketSSH 1.0
import "../components"

// The terminal for sessionManager.current, with the tab strip above and the extra keys
// below (sitting on top of the keyboard when it is open).
Page {
    id: page

    objectName: "terminalPage"
    allowedOrientations: Orientation.All
    showNavigationIndicator: false

    property QtObject session: sessionManager.current
    readonly property bool connected: session !== null && session.state === TerminalSession.Connected
    readonly property bool closed: session !== null && session.state === TerminalSession.Closed

    // The page needs no keyboard handling of its own: Silica's ApplicationWindow shrinks
    // the page to the space above the keyboard, so the extra keys sit right on top of it.

    function showKeyboard() {
        terminalItem.forceActiveFocus()
        Qt.inputMethod.show()
    }

    function hideKeyboard() {
        Qt.inputMethod.hide()
        terminalItem.focus = false
    }

    // Host key and password questions are separate dialogs, opened when this page is
    // on top and the page stack is idle.
    function showPrompt() {
        if (!session || session.prompt === TerminalSession.NoPrompt)
            return
        if (status !== PageStatus.Active || pageStack.busy)
            return
        if (session.prompt === TerminalSession.HostKeyPrompt)
            pageStack.push(Qt.resolvedUrl("HostKeyDialog.qml"), { session: session })
        else
            pageStack.push(Qt.resolvedUrl("PasswordDialog.qml"), { session: session })
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            if (session && session.prompt !== TerminalSession.NoPrompt)
                promptTimer.restart()
            else if (connected)
                showKeyboard()
        } else if (status === PageStatus.Deactivating) {
            actionsMenu.close()
        }
    }

    onSessionChanged: {
        terminalItem.clearSelection()
        if (session && session.prompt !== TerminalSession.NoPrompt)
            promptTimer.restart()
    }

    onConnectedChanged: {
        if (connected && status === PageStatus.Active)
            showKeyboard()
        else if (!connected)
            hideKeyboard()
    }

    Timer {
        id: promptTimer
        interval: 1
        onTriggered: page.showPrompt()
    }

    Connections {
        target: session
        onPromptChanged: promptTimer.restart()
        onBell: if (appSettings.visualBell) bellFlash.restart()
        onCopiedToClipboard: app.notify(qsTr("Copied to clipboard by the remote program"))
    }

    Connections {
        target: pageStack
        onBusyChanged: if (!pageStack.busy) promptTimer.restart()
    }

    Connections {
        target: sessionManager
        onCountChanged: {
            if (sessionManager.count === 0 && page.status === PageStatus.Active)
                pageStack.pop()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0D0F14"
    }

    TabStrip {
        id: strip
        width: parent.width
        z: 2
        onBackClicked: pageStack.pop()
        onNewTabClicked: pageStack.pop()
        onMenuClicked: actionsMenu.active ? actionsMenu.close() : actionsMenu.open(menuHost)
    }

    TerminalItem {
        id: terminalItem

        anchors {
            top: strip.bottom
            left: parent.left
            right: parent.right
            bottom: keys.visible ? keys.top : parent.bottom
        }
        terminal: session ? session.terminal : null
        margin: Theme.paddingSmall
        onClicked: {
            actionsMenu.close()
            if (connected)
                showKeyboard()
        }
        onLinkActivated: Qt.openUrlExternally(url)
        onFontPixelSizeChanged: appSettings.fontPixelSize = fontPixelSize
        Component.onCompleted: {
            if (appSettings.fontPixelSize > 0)
                fontPixelSize = appSettings.fontPixelSize
        }
    }

    // Bell: a short flash over the terminal.
    Rectangle {
        anchors.fill: terminalItem
        color: "#DADFE8"
        opacity: 0
        SequentialAnimation on opacity {
            id: bellFlash
            running: false
            NumberAnimation { to: 0.15; duration: 40 }
            NumberAnimation { to: 0; duration: 160 }
        }
    }

    // Selection: copy button.
    Button {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: terminalItem.bottom
            bottomMargin: Theme.paddingLarge
        }
        z: 2
        visible: terminalItem.hasSelection
        text: qsTr("Copy")
        onClicked: {
            terminalItem.copySelection()
            app.notify(qsTr("Copied"))
        }
    }

    // Scrolled back: jump to the live screen.
    IconButton {
        anchors {
            right: parent.right
            bottom: terminalItem.bottom
            margins: Theme.paddingLarge
        }
        z: 2
        visible: terminalItem.scrolledBack && !terminalItem.hasSelection
        icon.source: "image://theme/icon-m-page-down?#DADFE8"
        onClicked: terminalItem.scrollToBottom()
    }

    // Closed session: reconnect or close the tab.
    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            bottom: terminalItem.bottom
        }
        z: 2
        visible: closed && !terminalItem.hasSelection
        height: closedColumn.height + 2 * Theme.paddingLarge
        color: Qt.rgba(0.086, 0.098, 0.125, 0.92)

        Column {
            id: closedColumn
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "#DADFE8"
                font.pixelSize: Theme.fontSizeSmall
                text: session && session.closeReason !== "" ? session.closeReason : qsTr("Disconnected")
            }

            ButtonLayout {
                width: parent.width
                Button {
                    text: qsTr("Reconnect")
                    onClicked: session.reconnect()
                }
                Button {
                    text: qsTr("Close tab")
                    onClicked: sessionManager.close(sessionManager.currentIndex)
                }
            }
        }
    }

    ExtraKeys {
        id: keys
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        visible: appSettings.extraKeys && connected
        terminal: session ? session.terminal : null
        onHideKeyboard: page.hideKeyboard()
    }

    // The session menu opens below the tab strip, over the terminal.
    Item {
        id: menuHost
        anchors.top: strip.bottom
        width: parent.width
        height: actionsMenu.active ? actionsMenu.height : 0
        z: 3

        Rectangle {
            anchors.fill: parent
            color: "#161920"
            visible: actionsMenu.active
        }
    }

    ContextMenu {
        id: actionsMenu

        MenuItem {
            text: qsTr("Paste")
            visible: connected && Clipboard.hasText
            onClicked: terminalItem.pasteClipboard()
        }
        MenuItem {
            text: qsTr("Show keyboard")
            visible: connected && !Qt.inputMethod.visible
            onClicked: showKeyboard()
        }
        MenuItem {
            text: qsTr("Larger text")
            onClicked: terminalItem.fontPixelSize = terminalItem.fontPixelSize + 2
        }
        MenuItem {
            text: qsTr("Smaller text")
            onClicked: terminalItem.fontPixelSize = terminalItem.fontPixelSize - 2
        }
        MenuItem {
            text: qsTr("Disconnect")
            visible: session !== null && !closed
            onClicked: session.disconnectFromHost()
        }
        MenuItem {
            text: qsTr("Reconnect")
            visible: closed
            onClicked: session.reconnect()
        }
        MenuItem {
            text: qsTr("Close tab")
            visible: session !== null
            onClicked: sessionManager.close(sessionManager.currentIndex)
        }
    }
}
