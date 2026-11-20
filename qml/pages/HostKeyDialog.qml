import QtQuick 2.0
import Sailfish.Silica 1.0
import PocketSSH 1.0

// Asks whether to trust a server's host key: on first connect, or loudly when it changed.
Dialog {
    id: dialog

    property QtObject session
    // Copied when the dialog opens: the session clears its prompt as soon as it is answered.
    property var request: ({})
    readonly property bool changed: request.previousFingerprint !== undefined

    property bool answered: false

    function answer(trust) {
        if (answered || !session)
            return
        answered = true
        session.answerHostKey(trust)
    }

    Component.onCompleted: request = session.promptData
    onAccepted: answer(true)
    onRejected: answer(false)

    // The prompt may also have gone while this dialog was still sliding in.
    onStatusChanged: {
        if (status === PageStatus.Active && !answered && (!session || session.prompt !== TerminalSession.HostKeyPrompt)) {
            answered = true
            pageStack.pop()
        }
    }

    // The session gave up (closed) while this was open.
    Connections {
        target: session
        onPromptChanged: {
            if (session.prompt !== TerminalSession.HostKeyPrompt && !answered && dialog.status === PageStatus.Active) {
                answered = true
                pageStack.pop()
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                acceptText: changed ? qsTr("Trust new key") : qsTr("Trust")
                cancelText: qsTr("Cancel")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraLarge
                font.family: Theme.fontFamilyHeading
                color: changed ? "#FF6B6B" : Theme.highlightColor
                text: changed ? qsTr("Host key changed!") : qsTr("Unknown host")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: changed
                      ? qsTr("The key for %1 is different from the one you trusted before. "
                           + "This could mean someone is intercepting the connection. "
                           + "Only continue if you know the server's key was changed.").arg(request.address)
                      : qsTr("The authenticity of %1 can't be established. "
                           + "Check the fingerprint before trusting it.").arg(request.address)
            }

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                spacing: Theme.paddingSmall

                Label {
                    text: request.keyType || ""
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                }
                Label {
                    width: parent.width
                    text: request.fingerprint || ""
                    wrapMode: Text.WrapAnywhere
                    font.family: "Monospace"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.primaryColor
                }
                Label {
                    visible: changed
                    width: parent.width
                    text: qsTr("Previously trusted:") + "\n" + (request.previousFingerprint || "")
                    wrapMode: Text.WrapAnywhere
                    font.family: "Monospace"
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
