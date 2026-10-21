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
            description: app.syncing ? qsTr("Refreshing…") : ""
        }

        PullDownMenu {
            busy: app.syncing

            MenuItem {
                text: articles.unreadOnly ? qsTr("Show all articles") : qsTr("Show unread only")
                onClicked: articles.unreadOnly = !articles.unreadOnly
            }

            MenuItem {
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
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
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

            onClicked: pageStack.push(Qt.resolvedUrl("ArticlePage.qml"), { articleId: model.articleId })
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: articles.unreadOnly ? qsTr("No unread articles") : qsTr("No articles")
            hintText: articles.unreadOnly ? qsTr("Pull down to show all articles") : ""
        }

        VerticalScrollDecorator { }
    }
}
