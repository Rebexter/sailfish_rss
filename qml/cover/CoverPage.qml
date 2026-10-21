import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    CoverPlaceholder {
        text: app.loggedIn ? qsTr("%n unread", "", app.unreadCount) : "SailRSS"
        icon.source: "/usr/share/icons/hicolor/86x86/apps/harbour-sailrss.png"
    }

    CoverActionList {
        enabled: app.loggedIn && !app.syncing

        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: app.sync()
        }
    }
}
