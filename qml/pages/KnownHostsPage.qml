import QtQuick 2.0
import Sailfish.Silica 1.0

// Host keys trusted so far. Forgetting one makes the next connect ask again.
Page {
    id: page

    SilicaListView {
        id: list

        anchors.fill: parent
        model: knownHosts.entries()

        Connections {
            target: knownHosts
            onChanged: list.model = knownHosts.entries()
        }

        header: PageHeader {
            title: qsTr("Trusted host keys")
        }

        delegate: ListItem {
            id: item

            contentHeight: column.height + 2 * Theme.paddingMedium

            Column {
                id: column
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter

                Label {
                    width: parent.width
                    text: modelData.key
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: modelData.type
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
                Label {
                    width: parent.width
                    text: modelData.fingerprint
                    wrapMode: Text.WrapAnywhere
                    font.family: "Monospace"
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Forget")
                    onClicked: {
                        var key = modelData.key
                        item.remorseDelete(function () { knownHosts.remove(key) })
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: qsTr("No trusted keys")
            hintText: qsTr("A server's key is added here when you trust it on first connect")
        }

        VerticalScrollDecorator {}
    }
}
