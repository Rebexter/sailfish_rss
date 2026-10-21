import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sailrss 1.0

// Without folderId: "All articles", "Starred" and the folders.
// With folderId: that folder and its feeds.
Page {
    id: page

    property string folderId
    property string title: "SailRSS"
    readonly property bool topLevel: folderId.length === 0

    allowedOrientations: Orientation.All

    StreamListModel {
        id: streams

        controller: app
        folderId: page.folderId
    }

    RemorsePopup {
        id: remorse
    }

    SilicaListView {
        id: list

        anchors.fill: parent
        model: streams

        header: PageHeader {
            title: page.title
            description: {
                if (app.syncing)
                    return qsTr("Refreshing…")
                if (app.syncError.length > 0)
                    return app.syncError
                if (app.lastSync.getTime() > 0)
                    return qsTr("Updated %1").arg(Format.formatDate(app.lastSync, Formatter.DurationElapsed))
                return ""
            }
        }

        PullDownMenu {
            busy: app.syncing

            MenuItem {
                visible: page.topLevel
                text: qsTr("Log out")
                onClicked: remorse.execute(qsTr("Logging out"), function() { app.logout() })
            }

            MenuItem {
                text: qsTr("Refresh")
                enabled: !app.syncing
                onClicked: app.sync()
            }
        }

        delegate: ListItem {
            id: item

            contentHeight: Theme.itemSizeSmall

            Label {
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: countLabel.left
                    rightMargin: Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }
                text: model.title
                textFormat: Text.PlainText
                truncationMode: TruncationMode.Fade
                color: item.highlighted ? Theme.highlightColor
                                        : (model.count > 0 ? Theme.primaryColor : Theme.secondaryColor)
            }

            Label {
                id: countLabel

                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: model.count > 0 ? model.count : ""
                color: item.highlighted ? Theme.highlightColor : Theme.secondaryColor
            }

            onClicked: {
                if (model.kind === StreamListModel.FolderKind)
                    pageStack.push(Qt.resolvedUrl("StreamsPage.qml"), { folderId: model.streamId, title: model.title })
                else
                    pageStack.push(Qt.resolvedUrl("ArticleListPage.qml"), { streamId: model.streamId, title: model.title })
            }
        }

        ViewPlaceholder {
            // The two fixed entries are always there.
            enabled: page.topLevel && list.count <= 2
            text: app.syncing ? qsTr("Fetching your feeds…") : qsTr("No feeds")
            hintText: app.syncing ? "" : qsTr("Pull down to refresh")
        }

        VerticalScrollDecorator { }
    }
}
