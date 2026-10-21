#include "articlehtml.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace ArticleHtml {

namespace {

const auto Options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;

QString unescape(QString text)
{
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));  // last, so "&amp;lt;" stays "&lt;"
    return text;
}

// The value of an attribute in a tag such as <img src="...">, or empty.
QString attribute(const QString &tag, const QString &name)
{
    const QRegularExpression pattern(
        QStringLiteral("\\s%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(tag);
    for (int i = 1; i <= 3; ++i) {
        if (match.capturedStart(i) >= 0)
            return unescape(match.captured(i).trimmed());
    }
    return QString();
}

QUrl resolve(const QUrl &baseUrl, const QString &address)
{
    const QUrl url(address);
    return baseUrl.isValid() ? baseUrl.resolved(url) : url;
}

// Lazy-loading pages put the real address in data-src or srcset.
QString imageAddress(const QString &tag)
{
    for (const char *name : {"data-src", "data-lazy-src", "src"}) {
        const QString value = attribute(tag, QLatin1String(name));
        if (!value.isEmpty() && !value.startsWith(QLatin1String("data:")))
            return value;
    }
    const QString srcset = attribute(tag, QStringLiteral("srcset"));
    return srcset.section(QLatin1Char(','), 0, 0).trimmed().section(QLatin1Char(' '), 0, 0);
}

bool isTrackingPixel(const QString &tag)
{
    bool hasWidth = false;
    bool hasHeight = false;
    const int width = attribute(tag, QStringLiteral("width")).toInt(&hasWidth);
    const int height = attribute(tag, QStringLiteral("height")).toInt(&hasHeight);
    return hasWidth && hasHeight && width <= 2 && height <= 2;
}

// Where an image at the end of `before` links to: the href of an <a> that
// is still open there.
QUrl enclosingLink(const QString &before, const QUrl &baseUrl)
{
    static const QRegularExpression open(QStringLiteral("<a\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression close(QStringLiteral("</a\\s*>"), QRegularExpression::CaseInsensitiveOption);
    const int lastOpen = before.lastIndexOf(open);
    if (lastOpen < 0 || before.lastIndexOf(close) > lastOpen)
        return QUrl();
    const QString tag = open.match(before, lastOpen).captured(0);
    const QString href = attribute(tag, QStringLiteral("href"));
    return href.isEmpty() ? QUrl() : resolve(baseUrl, href);
}

// Makes link targets absolute, so tapping a relative link works.
QString resolveLinks(const QString &html, const QUrl &baseUrl)
{
    static const QRegularExpression href(QStringLiteral("(<a\\b[^>]*?\\shref\\s*=\\s*)(\"[^\"]*\"|'[^']*')"),
                                         QRegularExpression::CaseInsensitiveOption);
    QString result;
    int position = 0;
    QRegularExpressionMatchIterator it = href.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString quoted = match.captured(2);
        const QString target = resolve(baseUrl, unescape(quoted.mid(1, quoted.size() - 2))).toString();
        result += html.midRef(position, match.capturedStart(2) - position);
        result += QLatin1Char('"') + target.toHtmlEscaped() + QLatin1Char('"');
        position = match.capturedEnd(2);
    }
    result += html.midRef(position);
    return result;
}

QString prepare(const QString &html)
{
    // Removed with their content
    static const QRegularExpression blocks(
        QStringLiteral("<(script|style|noscript|template|object|svg|video|audio)\\b.*?</\\1\\s*>"), Options);
    // Removed, content kept (or they have none)
    static const QRegularExpression tags(
        QStringLiteral("</?(picture|source|embed|object|video|audio)\\b[^>]*>"), Options);
    static const QRegularExpression iframes(QStringLiteral("<iframe\\b[^>]*>(.*?</iframe\\s*>)?"), Options);

    QString cleaned = html;
    cleaned.remove(blocks);
    cleaned.remove(tags);

    // Embedded content (mostly videos) becomes a link to it.
    QString result;
    int position = 0;
    QRegularExpressionMatchIterator it = iframes.globalMatch(cleaned);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += cleaned.midRef(position, match.capturedStart() - position);
        const QString src = attribute(match.captured(0).section(QLatin1Char('>'), 0, 0), QStringLiteral("src"));
        if (!src.isEmpty()) {
            result += QStringLiteral("<p><a href=\"%1\">▶ %2</a></p>")
                          .arg(src.toHtmlEscaped(), QCoreApplication::translate("ArticleHtml", "Embedded content"));
        }
        position = match.capturedEnd();
    }
    result += cleaned.midRef(position);
    return result;
}

}  // namespace

QList<Block> blocks(const QString &html, const QUrl &baseUrl, const QString &linkColor)
{
    static const QRegularExpression images(QStringLiteral("<img\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QString style = QStringLiteral("<style>a { color: %1; }</style>").arg(linkColor);
    const QString prepared = prepare(html);

    QList<Block> result;
    const auto addText = [&](int from, int to) {
        const QString text = prepared.mid(from, to - from);
        if (text.trimmed().isEmpty())
            return;
        Block block;
        block.html = style + resolveLinks(text, baseUrl).trimmed();
        result.append(block);
    };

    int position = 0;
    QRegularExpressionMatchIterator it = images.globalMatch(prepared);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        addText(position, match.capturedStart());
        position = match.capturedEnd();

        const QString tag = match.captured(0);
        const QString address = imageAddress(tag);
        if (address.isEmpty() || isTrackingPixel(tag))
            continue;
        Block block;
        block.type = Block::Type::Image;
        block.image = resolve(baseUrl, address);
        block.link = enclosingLink(prepared.left(match.capturedStart()), baseUrl);
        result.append(block);
    }
    addText(position, prepared.size());
    return result;
}

}  // namespace ArticleHtml
