import QtQuick 2.0
import Sailfish.Silica 1.0

// A short message that fades in at the top of the window and goes away by itself.
Rectangle {
    id: toast

    function show(text) {
        label.text = text
        opacity = 1
        hideTimer.restart()
    }

    z: 1000
    anchors {
        top: parent.top
        topMargin: Theme.paddingLarge
        horizontalCenter: parent.horizontalCenter
    }
    width: Math.min(parent.width - 2 * Theme.horizontalPageMargin, label.implicitWidth + 2 * Theme.paddingLarge)
    height: label.height + 2 * Theme.paddingMedium
    radius: Theme.paddingSmall
    color: Theme.rgba(Theme.highlightDimmerColor, 0.95)
    border.color: Theme.rgba(Theme.highlightColor, 0.4)
    opacity: 0
    visible: opacity > 0

    Behavior on opacity { FadeAnimator {} }

    Label {
        id: label
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeSmall
    }

    MouseArea {
        anchors.fill: parent
        onClicked: toast.opacity = 0
    }

    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: toast.opacity = 0
    }
}
