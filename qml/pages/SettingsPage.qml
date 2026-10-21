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
