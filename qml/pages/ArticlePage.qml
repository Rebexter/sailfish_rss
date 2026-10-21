import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    property string articleId
    readonly property var article: app.article(articleId)
    readonly property var blocks: app.articleBlocks(articleId, Theme.highlightColor.toString())
    property bool read: article.read === true
    property bool starred: article.starred === true

    allowedOrientations: Orientation.All

    // Opening an article marks it read.
    Component.onCompleted: {
        if (article.id && !read)
            app.setRead(articleId, true)
    }

    Connections {
        target: app
        onArticleChanged: {
            if (itemId === page.articleId) {
                page.read = read
                page.starred = starred
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        // Room to scroll the last lines up to the middle of the screen
        contentHeight: column.height + height / 2

        PullDownMenu {
            MenuItem {
                text: qsTr("Open in browser")
                enabled: !!page.article.url
                onClicked: Qt.openUrlExternally(page.article.url)
            }
            MenuItem {
                text: page.starred ? qsTr("Remove star") : qsTr("Star")
                onClicked: app.setStarred(page.articleId, !page.starred)
            }
            MenuItem {
                text: page.read ? qsTr("Mark as unread") : qsTr("Mark as read")
                onClicked: app.setRead(page.articleId, !page.read)
            }
        }

        Column {
            id: column

            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            spacing: Theme.paddingMedium

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Label {
                width: parent.width
                text: page.article.title || ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                font.family: Theme.fontFamilyHeading
                color: Theme.highlightColor
            }

            Label {
                width: parent.width
                text: {
                    var parts = [page.article.feedTitle, page.article.author]
                    if (page.article.published)
                        parts.push(Format.formatDate(page.article.published, Formatter.DateMedium))
                    return parts.filter(function(part) { return !!part }).join(" · ")
                }
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
            }

            // Text and images, as split up by ArticleHtml::blocks()
            Repeater {
                model: page.blocks

                delegate: Item {
                    readonly property bool isImage: modelData.type === "image"

                    width: column.width
                    height: isImage ? imageBlock.height : textBlock.height

                    Label {
                        id: textBlock

                        visible: !parent.isImage
                        width: parent.width
                        text: visible ? modelData.html : ""
                        textFormat: Text.RichText
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        onLinkActivated: Qt.openUrlExternally(link)
                    }

                    Item {
                        id: imageBlock

                        // Hidden until loaded; offline, uncached images stay hidden.
                        visible: parent.isImage && image.status === Image.Ready
                        width: parent.width
                        height: visible ? image.height + 2 * Theme.paddingSmall : 0

                        Image {
                            id: image

                            // At most its own size, in CSS pixels like a browser,
                            // and never wider than the page.
                            y: Theme.paddingSmall
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(implicitWidth * Theme.pixelRatio, parent.width)
                            height: implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0
                            source: parent.parent.isImage ? modelData.image : ""
                            asynchronous: true
                            smooth: true
                            fillMode: Image.PreserveAspectFit
                        }

                        MouseArea {
                            anchors.fill: image
                            enabled: !!modelData.link
                            onClicked: Qt.openUrlExternally(modelData.link)
                        }
                    }
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
