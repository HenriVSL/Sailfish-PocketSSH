import QtQuick 2.0
import Sailfish.Silica 1.0

// Exports hosts to ~/Documents in the Pocket Shell format.
Dialog {
    id: dialog

    // Host ids to export; empty = all hosts.
    property var hostIds: []

    onAccepted: {
        var result = configTransfer.exportHosts(hostIds, secretsSwitch.checked)
        app.notify(result.ok ? qsTr("Saved to Documents/%1").arg(result.fileName) : result.error)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            DialogHeader {
                title: hostIds.length === 1 ? qsTr("Export host") : qsTr("Export all hosts")
                acceptText: qsTr("Export")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("The hosts are saved as a JSON file in Documents. "
                         + "Pocket Shell on iOS and PocketSSH can import it.")
            }

            TextSwitch {
                id: secretsSwitch
                text: qsTr("Include passwords and keys")
                description: checked
                             ? qsTr("Passwords, private keys and passphrases are written in plain text. "
                                  + "Anyone who gets the file can sign in to these servers.")
                             : qsTr("Only names, addresses and user names are exported.")
            }
        }
    }
}
