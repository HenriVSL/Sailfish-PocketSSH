import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("Terminal")
            }

            TextSwitch {
                text: qsTr("Extra keys")
                description: qsTr("Esc, Ctrl, Alt, Tab, arrows and paging keys above the keyboard")
                checked: appSettings.extraKeys
                onClicked: appSettings.extraKeys = checked
            }

            TextSwitch {
                text: qsTr("Visual bell")
                description: qsTr("Flash the terminal when a program rings the bell")
                checked: appSettings.visualBell
                onClicked: appSettings.visualBell = checked
            }

            ValueButton {
                label: qsTr("Text size")
                value: appSettings.fontPixelSize > 0 ? qsTr("%1 px").arg(Math.round(appSettings.fontPixelSize))
                                                      : qsTr("Default")
                description: qsTr("Pinch the terminal to zoom. Tap to reset.")
                onClicked: appSettings.fontPixelSize = 0
            }

            SectionHeader {
                text: qsTr("Security")
            }

            ValueButton {
                label: qsTr("Trusted host keys")
                value: knownHosts.count
                onClicked: pageStack.push(Qt.resolvedUrl("KnownHostsPage.qml"))
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Saved passwords, keys and passphrases are kept in a file only PocketSSH can read, "
                         + "inside its sandboxed storage on this device. They are never sent anywhere except "
                         + "to the server you connect to.")
            }

            SectionHeader {
                text: qsTr("About")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                textFormat: Text.StyledText
                linkColor: Theme.primaryColor
                text: qsTr("PocketSSH %1").arg(appVersion) + "<br><br>"
                      + qsTr("An open-source SSH client for Sailfish OS, ported from Pocket Shell for iOS. "
                           + "Uses libssh2 (BSD-3-Clause) with OpenSSL, and libvterm (MIT).") + "<br><br>"
                      + "<a href=\"https://github.com/HenriVSL/Sailfish-PocketSSH\">github.com/HenriVSL/Sailfish-PocketSSH</a>"
                onLinkActivated: Qt.openUrlExternally(link)
            }
        }

        VerticalScrollDecorator {}
    }
}
