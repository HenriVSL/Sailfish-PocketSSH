import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"
import "components"

ApplicationWindow {
    id: app

    initialPage: Component { HostListPage {} }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: Orientation.All

    // Brings the terminal page up, showing sessionManager.current.
    function showTerminal() {
        var top = pageStack.currentPage
        if (top && top.objectName === "terminalPage")
            return
        var existing = pageStack.find(function (page) { return page.objectName === "terminalPage" })
        if (existing)
            pageStack.pop(existing)
        else
            pageStack.push(Qt.resolvedUrl("pages/TerminalPage.qml"))
    }

    // Opens a new session for the host and shows it.
    function connectTo(hostId) {
        if (sessionManager.open(hostId))
            showTerminal()
    }

    function notify(text) {
        toast.show(text)
    }

    Toast {
        id: toast
    }
}
