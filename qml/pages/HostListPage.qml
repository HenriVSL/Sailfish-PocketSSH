import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import "../components"

Page {
    id: page

    SilicaListView {
        id: list

        anchors.fill: parent
        model: hostModel

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Import hosts")
                onClicked: pageStack.push(importPicker)
            }
            MenuItem {
                text: qsTr("Export all hosts")
                visible: hostModel.count > 0
                onClicked: pageStack.push(Qt.resolvedUrl("ExportDialog.qml"), { hostIds: [] })
            }
            MenuItem {
                text: qsTr("Add host")
                onClicked: pageStack.push(Qt.resolvedUrl("HostDialog.qml"))
            }
        }

        header: Column {
            width: list.width

            PageHeader {
                title: "PocketSSH"
            }

            SectionHeader {
                text: qsTr("Sessions")
                visible: sessionManager.count > 0
            }

            Repeater {
                model: sessionManager

                SessionListItem {
                    width: list.width
                    onClicked: {
                        sessionManager.currentIndex = index
                        app.showTerminal()
                    }
                }
            }

            SectionHeader {
                text: qsTr("Hosts")
                visible: sessionManager.count > 0 && hostModel.count > 0
            }
        }

        delegate: ListItem {
            id: item

            contentHeight: Theme.itemSizeMedium
            onClicked: app.connectTo(hostId)

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: displayName
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: address + (authMethod === "privateKey" ? " · " + qsTr("key") : "")
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Edit")
                    onClicked: pageStack.push(Qt.resolvedUrl("HostDialog.qml"), { hostId: hostId })
                }
                MenuItem {
                    text: qsTr("Duplicate")
                    onClicked: hostModel.duplicate(hostId)
                }
                MenuItem {
                    text: qsTr("Export")
                    onClicked: pageStack.push(Qt.resolvedUrl("ExportDialog.qml"), { hostIds: [hostId] })
                }
                MenuItem {
                    text: qsTr("Move up")
                    visible: index > 0
                    onClicked: hostModel.move(index, index - 1)
                }
                MenuItem {
                    text: qsTr("Move down")
                    visible: index < hostModel.count - 1
                    onClicked: hostModel.move(index, index + 1)
                }
                MenuItem {
                    text: qsTr("Delete")
                    onClicked: {
                        var id = hostId
                        item.remorseDelete(function () { hostModel.remove(id) })
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: hostModel.count === 0 && sessionManager.count === 0
            text: qsTr("No hosts yet")
            hintText: qsTr("Pull down to add a host, or import hosts exported from Pocket Shell")
        }

        VerticalScrollDecorator {}
    }

    Component {
        id: importPicker

        FilePickerPage {
            title: qsTr("Import hosts")
            nameFilters: ["*.json"]
            onSelectedContentPropertiesChanged: {
                var result = configTransfer.importFile(selectedContentProperties.filePath)
                if (!result.ok)
                    app.notify(result.error)
                else
                    app.notify(result.count === 1 ? qsTr("Imported 1 host") : qsTr("Imported %1 hosts").arg(result.count))
            }
        }
    }
}
