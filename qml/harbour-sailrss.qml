import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    initialPage: app.loggedIn ? streamsPage : loginPage
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    Component {
        id: streamsPage
        StreamsPage { }
    }

    Component {
        id: loginPage
        LoginPage { }
    }

    Connections {
        target: app
        // Logged out, or the session expired during a sync
        onAccountChanged: {
            if (!app.loggedIn)
                pageStack.replaceAbove(null, loginPage)
        }
    }
}
