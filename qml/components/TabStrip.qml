import QtQuick 2.0
import Sailfish.Silica 1.0
import PocketSSH 1.0

// The bar above the terminal: back to the host list, one tab per session, new tab,
// and the session menu.
Rectangle {
    id: strip

    signal backClicked()
    signal newTabClicked()
    signal menuClicked()

    readonly property color textColor: "#DADFE8"

    height: Theme.itemSizeSmall
    color: "#161920"

    IconButton {
        id: backButton
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-back?" + strip.textColor
        onClicked: strip.backClicked()
    }

    ListView {
        id: tabs
        anchors {
            left: backButton.right
            right: newButton.left
            top: parent.top
            bottom: parent.bottom
        }
        orientation: ListView.Horizontal
        clip: true
        spacing: Theme.paddingSmall
        model: sessionManager
        currentIndex: sessionManager.currentIndex
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 150

        delegate: BackgroundItem {
            id: tab

            readonly property bool selected: index === sessionManager.currentIndex

            width: Math.min(Math.max(row.implicitWidth + 2 * Theme.paddingMedium, Theme.itemSizeLarge),
                            Theme.itemSizeHuge * 1.4)
            height: tabs.height
            highlightedColor: Qt.rgba(0.29, 0.79, 0.64, 0.25)
            onClicked: sessionManager.currentIndex = index

            Rectangle {
                anchors {
                    fill: parent
                    topMargin: Theme.paddingSmall
                    bottomMargin: Theme.paddingSmall
                }
                radius: height / 2
                color: tab.selected ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                border.width: tab.selected ? 1 : 0
                border.color: Qt.rgba(0.29, 0.79, 0.64, 0.7)
            }

            Row {
                id: row
                anchors.centerIn: parent
                spacing: Theme.paddingSmall

                StatusDot {
                    height: Theme.paddingMedium
                    state: sessionState
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    width: Math.min(implicitWidth, Theme.itemSizeHuge * 1.4 - 3 * Theme.paddingMedium)
                    text: title
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    font.bold: tab.selected
                    color: strip.textColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    IconButton {
        id: newButton
        anchors {
            right: menuButton.left
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-tab-new?" + strip.textColor
        onClicked: strip.newTabClicked()
    }

    IconButton {
        id: menuButton
        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-menu?" + strip.textColor
        onClicked: strip.menuClicked()
    }
}
