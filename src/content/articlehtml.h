#ifndef SAILRSS_ARTICLEHTML_H
#define SAILRSS_ARTICLEHTML_H

#include <QList>
#include <QString>
#include <QUrl>

namespace ArticleHtml {

// A piece of an article: rich text, or an image between the text.
struct Block {
    enum class Type {
        Text,
        Image,
    };
    Type type = Type::Text;
    QString html;  // Text: HTML for a QML Text with Text.RichText
    QUrl image;    // Image: absolute image address
    QUrl link;     // Image: where the image links to, if anywhere
};

// Splits article HTML into text and image blocks. QML's rich text cannot
// scale images to the page width, so images become separate blocks that
// QML Image items can scale. Scripts, styles and embedded content are
// dropped; embedded iframes become a link. Relative image and link
// addresses are resolved against baseUrl (the article's address).
QList<Block> blocks(const QString &html, const QUrl &baseUrl, const QString &linkColor);

}  // namespace ArticleHtml

#endif  // SAILRSS_ARTICLEHTML_H
