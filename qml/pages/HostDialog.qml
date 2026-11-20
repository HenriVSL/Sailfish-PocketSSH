import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0

// Add or edit a host. Secrets are never shown: a saved password or passphrase stays as
// it is unless something new is typed or "Forget" is chosen.
Dialog {
    id: dialog

    property string hostId: ""
    readonly property var original: hostId !== "" ? hostModel.get(hostId) : ({})
    readonly property bool usesKey: authCombo.currentIndex === 1

    // A key chosen or pasted in this dialog ("" = keep the saved one).
    property string newKey: ""
    property var newKeyInfo: null
    property bool forgetPassword: false
    property bool forgetKey: false

    readonly property bool hasKey: newKey !== "" || (original.hasPrivateKey === true && !forgetKey)
    readonly property bool portValid: portField.acceptableInput

    canAccept: hostnameField.text.trim().length > 0 && portValid

    function useKey(result) {
        if (!result.ok) {
            app.notify(result.error)
            return
        }
        newKey = result.key
        newKeyInfo = result
        forgetKey = false
        passphraseField.text = ""
    }

    function keyDescription() {
        var type, encrypted
        if (newKey !== "") {
            type = newKeyInfo.type
            encrypted = newKeyInfo.encrypted
        } else if (hasKey) {
            type = original.keyType
            encrypted = original.keyEncrypted
        } else {
            return qsTr("No key saved")
        }
        var text = type ? qsTr("%1 key").arg(type) : qsTr("Private key")
        if (encrypted)
            text += " · " + qsTr("protected with a passphrase")
        if (newKey !== "")
            text += " · " + qsTr("new")
        return text
    }

    onAccepted: {
        var id = hostModel.save({
            "id": hostId,
            "name": nameField.text.trim(),
            "hostname": hostnameField.text.trim(),
            "port": parseInt(portField.text, 10),
            "username": usernameField.text.trim(),
            "authMethod": usesKey ? "privateKey" : "password"
        })
        if (passwordField.text !== "")
            hostModel.setPassword(id, passwordField.text)
        else if (forgetPassword)
            hostModel.setPassword(id, "")

        if (newKey !== "")
            hostModel.setPrivateKey(id, newKey, passphraseField.text)
        else if (forgetKey)
            hostModel.setPrivateKey(id, "", "")
        else if (passphraseField.text !== "")
            hostModel.setPassphrase(id, passphraseField.text)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: hostId === "" ? qsTr("Add host") : qsTr("Edit host")
                acceptText: qsTr("Save")
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Name")
                placeholderText: qsTr("Name (optional)")
                text: original.name || ""
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: hostnameField.focus = true
            }

            TextField {
                id: hostnameField
                width: parent.width
                label: qsTr("Host name or IP address")
                placeholderText: label
                text: original.hostname || ""
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: portField.focus = true
            }

            TextField {
                id: portField
                width: parent.width
                label: portValid ? qsTr("Port") : qsTr("Port must be 1–65535")
                placeholderText: qsTr("Port")
                text: original.port !== undefined ? String(original.port) : "22"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: usernameField.focus = true
            }

            TextField {
                id: usernameField
                width: parent.width
                label: qsTr("User name")
                placeholderText: label
                text: original.username || ""
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            ComboBox {
                id: authCombo
                width: parent.width
                label: qsTr("Sign in with")
                currentIndex: original.authMethod === "privateKey" ? 1 : 0
                menu: ContextMenu {
                    MenuItem { text: qsTr("Password") }
                    MenuItem { text: qsTr("Private key") }
                }
            }

            // Password
            Column {
                width: parent.width
                visible: !usesKey

                PasswordField {
                    id: passwordField
                    width: parent.width
                    label: qsTr("Password")
                    placeholderText: original.hasPassword && !forgetPassword
                                     ? qsTr("Saved password (unchanged)")
                                     : qsTr("Ask when connecting")
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: original.hasPassword === true && !forgetPassword
                    text: qsTr("Forget saved password")
                    onClicked: {
                        forgetPassword = true
                        passwordField.text = ""
                    }
                }
            }

            // Private key
            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                visible: usesKey

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    text: keyDescription()
                    color: hasKey ? Theme.highlightColor : Theme.secondaryHighlightColor
                    wrapMode: Text.Wrap
                }

                ButtonLayout {
                    Button {
                        text: qsTr("Choose key file")
                        onClicked: pageStack.push(keyPicker)
                    }
                    Button {
                        text: qsTr("Paste key")
                        enabled: Clipboard.hasText
                        onClicked: {
                            var info = hostModel.inspectKey(Clipboard.text)
                            if (info.ok)
                                info.key = Clipboard.text
                            useKey(info)
                        }
                    }
                    Button {
                        text: qsTr("Remove key")
                        visible: hasKey
                        onClicked: {
                            newKey = ""
                            newKeyInfo = null
                            forgetKey = true
                            passphraseField.text = ""
                        }
                    }
                }

                PasswordField {
                    id: passphraseField
                    width: parent.width
                    visible: hasKey
                    label: qsTr("Key passphrase")
                    placeholderText: newKey === "" && original.hasPassphrase
                                     ? qsTr("Saved passphrase (unchanged)")
                                     : qsTr("Passphrase (empty: ask if needed)")
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Supports OpenSSH and PEM private keys (Ed25519, ECDSA, RSA). "
                             + "PocketSSH can open key files in Documents and Downloads. "
                             + "The key is copied into the app's private storage.")
                }
            }
        }

        VerticalScrollDecorator {}
    }

    Component {
        id: keyPicker

        FilePickerPage {
            title: qsTr("Choose private key")
            onSelectedContentPropertiesChanged: useKey(hostModel.readKeyFile(selectedContentProperties.filePath))
        }
    }
}
