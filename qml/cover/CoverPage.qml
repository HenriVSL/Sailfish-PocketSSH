import QtQuick 2.0
import Sailfish.Silica 1.0
import PocketSSH 1.0
import "../components"

// With a session open, the cover shows a live miniature of its terminal; otherwise the
// app name and how many hosts are saved.
CoverBackground {
    id: cover

    readonly property QtObject session: sessionManager.current

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium
        visible: !session

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "/usr/share/icons/hicolor/172x172/apps/harbour-pocketssh.png"
            width: Theme.iconSizeLarge
            height: width
            sourceSize.width: width
            sourceSize.height: height
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "PocketSSH"
            color: Theme.primaryColor
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: hostModel.count === 1 ? qsTr("1 host") : qsTr("%1 hosts").arg(hostModel.count)
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
    }

    Item {
        anchors.fill: parent
        visible: session !== null

        Row {
            id: titleRow
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: Theme.paddingMedium
            }
            spacing: Theme.paddingSmall

            StatusDot {
                height: Theme.paddingMedium
                state: session ? session.state : TerminalSession.Closed
                anchors.verticalCenter: parent.verticalCenter
            }

            Label {
                width: parent.width - Theme.paddingMedium - parent.spacing
                text: session ? session.title : ""
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.primaryColor
            }
        }

        Rectangle {
            anchors {
                top: titleRow.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: Theme.paddingSmall
                // Room for the cover action at the bottom.
                bottomMargin: sessionManager.count > 1 ? Theme.itemSizeSmall : Theme.paddingSmall
            }
            radius: Theme.paddingSmall
            color: "#0D0F14"
            clip: true

            TerminalItem {
                anchors.fill: parent
                terminal: session ? session.terminal : null
                resizesTerminal: false
                margin: Theme.paddingSmall
                enabled: false
            }
        }
    }

    CoverActionList {
        enabled: sessionManager.count > 1

        CoverAction {
            iconSource: "image://theme/icon-cover-next"
            onTriggered: sessionManager.selectNext()
        }
    }
}
