import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    allowedOrientations: Orientation.All

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("Refresh")
            }

            ComboBox {
                // Minutes, matching the menu entries
                readonly property var intervals: [0, 5, 15, 30, 60, 120, 240]

                width: parent.width
                label: qsTr("Refresh automatically")
                description: qsTr("While SailRSS is open, also when it is in the background")
                currentIndex: Math.max(0, intervals.indexOf(app.refreshInterval))
                menu: ContextMenu {
                    MenuItem { text: qsTr("Off") }
                    MenuItem { text: qsTr("Every 5 minutes") }
                    MenuItem { text: qsTr("Every 15 minutes") }
                    MenuItem { text: qsTr("Every 30 minutes") }
                    MenuItem { text: qsTr("Every hour") }
                    MenuItem { text: qsTr("Every 2 hours") }
                    MenuItem { text: qsTr("Every 4 hours") }
                }
                onCurrentIndexChanged: app.refreshInterval = intervals[currentIndex]
            }

            TextSwitch {
                text: qsTr("Notify about new articles")
                description: qsTr("After a refresh while SailRSS is in the background")
                checked: app.notifyNewArticles
                onCheckedChanged: app.notifyNewArticles = checked
            }

            SectionHeader {
                text: qsTr("Storage")
            }

            ComboBox {
                // Days, matching the menu entries; 0 is no limit
                readonly property var windows: [7, 30, 90, 365, 730, 0]

                width: parent.width
                label: qsTr("Maximum article age")
                description: qsTr("Older articles are not downloaded, counted from when they arrived on the server. Starred articles are always downloaded. A longer time downloads the older articles on the next refresh.")
                currentIndex: Math.max(0, windows.indexOf(app.historyDays))
                menu: ContextMenu {
                    MenuItem { text: qsTr("1 week") }
                    MenuItem { text: qsTr("1 month") }
                    MenuItem { text: qsTr("3 months") }
                    MenuItem { text: qsTr("1 year") }
                    MenuItem { text: qsTr("2 years") }
                    MenuItem { text: qsTr("No limit") }
                }
                onCurrentIndexChanged: app.historyDays = windows[currentIndex]
            }

            ComboBox {
                // Days, matching the menu entries; 0 is forever
                readonly property var retentions: [1, 7, 30, 365, 0]

                width: parent.width
                label: qsTr("Keep read articles")
                description: qsTr("Counted from when an article was read. Unread and starred articles are always kept.")
                currentIndex: Math.max(0, retentions.indexOf(app.readRetention))
                menu: ContextMenu {
                    MenuItem { text: qsTr("1 day") }
                    MenuItem { text: qsTr("1 week") }
                    MenuItem { text: qsTr("1 month") }
                    MenuItem { text: qsTr("1 year") }
                    MenuItem { text: qsTr("Forever") }
                }
                onCurrentIndexChanged: app.readRetention = retentions[currentIndex]
            }

            SectionHeader {
                text: qsTr("Account")
            }

            DetailItem {
                label: qsTr("Server")
                value: app.serverUrl
            }

            DetailItem {
                label: qsTr("User name")
                value: app.userName
            }

            DetailItem {
                label: qsTr("Last refresh")
                value: app.lastSync.getTime() > 0 ? Format.formatDate(app.lastSync, Formatter.Timepoint)
                                                  : qsTr("Never")
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Log out")
                // Going back to the login page happens in the app window.
                onClicked: remorse.execute(qsTr("Logging out"), function() { app.logout() })
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Logging out removes the articles stored on this device; your feeds stay on the server. Changes that were not sent yet, for example while offline, are lost.")
            }
        }

        VerticalScrollDecorator { }
    }
}
