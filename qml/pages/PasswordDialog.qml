import QtQuick 2.0
import Sailfish.Silica 1.0
import PocketSSH 1.0

// Asks for the account password, or for the passphrase of the host's private key.
Dialog {
    id: dialog

    property QtObject session
    // Copied when the dialog opens: the session clears its prompt as soon as it is answered.
    property int expectedPrompt: TerminalSession.PasswordPrompt
    readonly property bool passphrase: expectedPrompt === TerminalSession.PassphrasePrompt
    property var request: ({})

    property bool answered: false

    function answer(secret) {
        if (answered || !session)
            return
        answered = true
        session.answerPassword(secret, rememberSwitch.checked)
    }

    canAccept: secretField.text.length > 0
    onAccepted: answer(secretField.text)
    onRejected: answer("")

    Component.onCompleted: {
        expectedPrompt = session.prompt
        request = session.promptData
    }

    // The prompt may also have gone while this dialog was still sliding in.
    onStatusChanged: {
        if (status === PageStatus.Active && !answered && (!session || session.prompt !== expectedPrompt)) {
            answered = true
            pageStack.pop()
        }
    }

    Connections {
        target: session
        onPromptChanged: {
            if (session.prompt !== expectedPrompt && !answered && dialog.status === PageStatus.Active) {
                answered = true
                pageStack.pop()
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            acceptText: qsTr("Connect")
            title: passphrase ? qsTr("Key passphrase") : qsTr("Password")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            text: passphrase ? qsTr("The private key for %1 is protected with a passphrase.").arg(request.address)
                             : qsTr("Password for %1").arg(request.address)
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            visible: text !== ""
            wrapMode: Text.Wrap
            color: "#FF6B6B"
            text: request.message || ""
        }

        PasswordField {
            id: secretField
            width: parent.width
            label: passphrase ? qsTr("Passphrase") : qsTr("Password")
            focus: true
            EnterKey.enabled: text.length > 0
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }

        TextSwitch {
            id: rememberSwitch
            text: passphrase ? qsTr("Save passphrase") : qsTr("Save password")
            description: qsTr("Kept in PocketSSH's private storage on this device")
        }
    }
}
