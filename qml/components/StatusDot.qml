import QtQuick 2.0
import PocketSSH 1.0

// Session state at a glance: amber while connecting, green when connected, grey when closed.
Rectangle {
    property int state: TerminalSession.Closed

    width: Math.round(height)
    height: 16
    radius: height / 2
    color: state === TerminalSession.Connected ? "#8FD18B"
         : state === TerminalSession.Connecting ? "#E9C46A" : "#6B7280"
}
