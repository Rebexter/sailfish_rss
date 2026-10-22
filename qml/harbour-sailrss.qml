import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.DBus 2.0
import Nemo.KeepAlive 1.2
import Nemo.Notifications 1.0
import "pages"

ApplicationWindow {
    id: window

    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive

    // Shows or updates the "new articles" notification after a sync in the
    // background; in the foreground the new articles count as seen.
    function syncFinished() {
        if (window.appActive) {
            app.markArticlesSeen()
            return
        }
        if (!app.notifyNewArticles)
            return
        var news = app.newArticles()
        if (news.count === 0 || news.count === newArticlesNotification.itemCount)
            return
        newArticlesNotification.summary = qsTr("%n new articles", "", news.count)
        newArticlesNotification.body = news.newestTitle
        newArticlesNotification.previewSummary = newArticlesNotification.summary
        newArticlesNotification.previewBody = newArticlesNotification.body
        newArticlesNotification.itemCount = news.count
        newArticlesNotification.publish()
    }

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
            if (!app.loggedIn) {
                newArticlesNotification.close()
                pageStack.replaceAbove(null, loginPage)
            }
        }
        onSyncingChanged: {
            if (app.syncing)
                return
            window.syncFinished()
            if (refreshJob.running)
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

    onAppActiveChanged: {
        if (!appActive)
            return
        // Back in the foreground: what arrived meanwhile is now seen.
        newArticlesNotification.close()
        newArticlesNotification.itemCount = 0
        app.markArticlesSeen()
        // Catch up after a long time in the background.
        if (app.syncDue())
            app.sync()
    }

    // One summary notification, updated by later syncs instead of adding more.
    Notification {
        id: newArticlesNotification

        appName: "SailRSS"
        appIcon: "/usr/share/icons/hicolor/86x86/apps/harbour-sailrss.png"
        // Tapping it calls activate() below.
        remoteActions: [{
            "name": "default",
            "service": "io.github.rebexter.harbour-sailrss",
            "path": "/",
            "iface": "io.github.rebexter.sailrss",
            "method": "activate"
        }]
    }

    // The sandbox lets the app own this bus name. There is no D-Bus
    // activation (not allowed in Harbour), so the notification is closed
    // when the app quits.
    DBusAdaptor {
        service: "io.github.rebexter.harbour-sailrss"
        path: "/"
        iface: "io.github.rebexter.sailrss"
        xml: "  <interface name=\"io.github.rebexter.sailrss\">\n"
             + "    <method name=\"activate\"/>\n"
             + "  </interface>\n"

        function activate() {
            window.activate()
        }
    }

    Component.onDestruction: newArticlesNotification.close()
}
