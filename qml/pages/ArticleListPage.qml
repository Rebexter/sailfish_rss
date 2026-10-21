import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sailrss 1.0

Page {
    id: page

    property string streamId
    property string title

    allowedOrientations: Orientation.All

    ArticleListModel {
        id: articles

        controller: app
        streamId: page.streamId
        oldestFirst: app.oldestFirst
    }

    RemorsePopup {
        id: remorse
    }

    SilicaListView {
        id: list

        anchors.fill: parent
        model: articles

        header: PageHeader {
            title: page.title
            description: {
                if (app.syncing)
                    return qsTr("Refreshing…")
                if (articles.filter === ArticleListModel.Unread)
                    return qsTr("Unread articles")
                if (articles.filter === ArticleListModel.Read)
                    return qsTr("Read articles")
                return qsTr("All articles")
            }
        }

        PullDownMenu {
            busy: app.syncing

            // The two views other than the current one
            MenuItem {
                visible: articles.filter !== ArticleListModel.Unread
                text: qsTr("Show unread articles")
                onClicked: articles.filter = ArticleListModel.Unread
            }

            MenuItem {
                visible: articles.filter !== ArticleListModel.All
                text: qsTr("Show all articles")
                onClicked: articles.filter = ArticleListModel.All
            }

            MenuItem {
                visible: articles.filter !== ArticleListModel.Read
                text: qsTr("Show read articles")
                onClicked: articles.filter = ArticleListModel.Read
            }

            MenuItem {
                text: app.oldestFirst ? qsTr("Show newest first") : qsTr("Show oldest first")
                onClicked: app.oldestFirst = !app.oldestFirst
            }

            MenuItem {
                visible: articles.filter !== ArticleListModel.Read
                text: qsTr("Mark all as read")
                enabled: list.count > 0
                onClicked: remorse.execute(qsTr("Marking all as read"), function() { articles.markAllRead() })
            }

            MenuItem {
                text: qsTr("Refresh")
                enabled: !app.syncing
                onClicked: app.sync()
            }
        }

        delegate: ListItem {
            id: item

            contentHeight: column.height + 2 * Theme.paddingMedium

            menu: ContextMenu {
                MenuItem {
                    text: model.read ? qsTr("Mark as unread") : qsTr("Mark as read")
                    onClicked: app.setRead(model.articleId, !model.read)
                }
                MenuItem {
                    text: model.starred ? qsTr("Remove star") : qsTr("Star")
                    onClicked: app.setStarred(model.articleId, !model.starred)
                }
            }

            Column {
                id: column

                anchors {
                    left: parent.left
                    right: readMarker.left
                    leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.title
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: item.highlighted ? Theme.highlightColor
                                            : (model.read ? Theme.secondaryColor : Theme.primaryColor)
                }

                Label {
                    width: parent.width
                    text: (model.starred ? "★ " : "") + model.feedTitle + " · "
                          + Format.formatDate(model.published, Formatter.TimepointRelative)
                    textFormat: Text.PlainText
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            // Tapping the marker toggles read without opening the article.
            MouseArea {
                id: readMarker

                anchors {
                    top: parent.top
                    bottom: parent.bottom
                    right: parent.right
                }
                width: Theme.itemSizeSmall

                Rectangle {
                    readonly property bool pressed: readMarker.pressed && readMarker.containsMouse

                    anchors.centerIn: parent
                    width: Theme.paddingLarge
                    height: width
                    radius: width / 2
                    color: model.read ? "transparent" : Theme.highlightColor
                    border.width: model.read ? Math.max(1, Math.round(Theme.paddingSmall / 3)) : 0
                    border.color: pressed ? Theme.highlightColor : Theme.secondaryColor
                    scale: pressed ? 1.3 : 1

                    Behavior on scale {
                        NumberAnimation { duration: 100 }
                    }
                }

                onClicked: app.setRead(model.articleId, !model.read)
            }

            onClicked: pageStack.push(Qt.resolvedUrl("ArticlePage.qml"), { articleId: model.articleId })
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: {
                if (articles.filter === ArticleListModel.Unread)
                    return qsTr("No unread articles")
                if (articles.filter === ArticleListModel.Read)
                    return qsTr("No read articles")
                return qsTr("No articles")
            }
            hintText: articles.filter === ArticleListModel.Unread ? qsTr("Pull down to show read articles") : ""
        }

        VerticalScrollDecorator { }
    }
}
