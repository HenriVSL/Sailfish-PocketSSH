import QtQuick 2.0
import Sailfish.Silica 1.0
import PocketSSH 1.0

// One open session in the host list (model: sessionManager).
ListItem {
    id: item

    contentHeight: Theme.itemSizeMedium

    StatusDot {
        id: dot
        state: sessionState
        anchors {
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
    }

    Column {
        anchors {
            left: dot.right
            leftMargin: Theme.paddingMedium
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }

        Label {
            width: parent.width
            text: title
            truncationMode: TruncationMode.Fade
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            text: {
                var status
                if (sessionState === TerminalSession.Connecting)
                    status = session.prompt !== TerminalSession.NoPrompt ? qsTr("waiting for you") : qsTr("connecting")
                else if (sessionState === TerminalSession.Connected)
                    status = qsTr("connected")
                else
                    status = qsTr("closed")
                return address + " · " + status
            }
        }
    }

    menu: ContextMenu {
        MenuItem {
            text: qsTr("Reconnect")
            visible: sessionState === TerminalSession.Closed
            onClicked: {
                session.reconnect()
                sessionManager.currentIndex = index
                app.showTerminal()
            }
        }
        MenuItem {
            text: qsTr("Disconnect")
            visible: sessionState !== TerminalSession.Closed
            onClicked: session.disconnectFromHost()
        }
        MenuItem {
            text: qsTr("Close")
            onClicked: sessionManager.close(index)
        }
    }
}
