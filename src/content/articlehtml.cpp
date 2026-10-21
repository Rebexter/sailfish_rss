#include "articlehtml.h"

#include <QRegularExpression>

namespace ArticleHtml {

QString forRichText(const QString &html, const QString &linkColor)
{
    const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;
    // Elements removed with their content
    static const QRegularExpression blocks(
        QStringLiteral("<(script|style|noscript|iframe|object|svg|video|audio)\\b.*?</\\1\\s*>"), options);
    // Tags removed, content kept (or they have none)
    static const QRegularExpression tags(
        QStringLiteral("</?(img|picture|source|embed|iframe|video|audio|object)\\b[^>]*>"), options);

    QString cleaned = html;
    cleaned.remove(blocks);
    cleaned.remove(tags);
    return QStringLiteral("<style>a { color: %1; }</style>").arg(linkColor) + cleaned.trimmed();
}

}  // namespace ArticleHtml
