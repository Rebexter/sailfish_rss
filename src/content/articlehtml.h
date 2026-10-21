#ifndef SAILRSS_ARTICLEHTML_H
#define SAILRSS_ARTICLEHTML_H

#include <QString>

namespace ArticleHtml {

// Prepares article HTML for a QML Text/Label with Text.RichText, which
// supports only a subset of HTML: drops scripts, styles and embedded
// content (they would show as text or not at all), drops images (they
// cannot be scaled to the page width), and colors links.
QString forRichText(const QString &html, const QString &linkColor);

}  // namespace ArticleHtml

#endif  // SAILRSS_ARTICLEHTML_H
