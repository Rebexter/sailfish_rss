import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.KeepAlive 1.2
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
        onSyncingChanged: {
            if (!app.syncing && refreshJob.running)
                refreshJob.finished()
        }
    }

    // Refreshes periodically while the app runs, also in the background.
    // The system wakes the device for it and lets it sleep again once
    // finished() is called, after the sync.
    BackgroundJob {
        id: refreshJob

        enabled: app.loggedIn && app.refreshInterval > 0
        frequency: app.refreshInterval * 60  // the Frequency values are seconds
        onTriggered: {
            app.sync()
            if (!app.syncing)
                finished()
        }
    }

    // Catch up when coming back to the foreground after a long time.
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive && app.syncDue())
                app.sync()
        }
    }
}
