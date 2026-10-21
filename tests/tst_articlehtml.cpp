#include "content/articlehtml.h"

#include <QtTest>

using namespace ArticleHtml;

namespace {

const QUrl Base("https://news.example.org/2026/article.html");
const QString Style = QStringLiteral("<style>a { color: #00ffff; }</style>");

QList<Block> split(const QString &html)
{
    return blocks(html, Base, QStringLiteral("#00ffff"));
}

// A compact description of the blocks, e.g. "T<p>a</p> | I:https://x/a.png"
QString describe(const QList<Block> &list)
{
    QStringList parts;
    for (const Block &block : list) {
        if (block.type == Block::Type::Text) {
            QString html = block.html;
            if (!html.startsWith(Style))
                return QStringLiteral("missing link style: ") + html;
            parts.append(QStringLiteral("T") + html.mid(Style.size()));
        } else {
            QString part = QStringLiteral("I:") + block.image.toString();
            if (!block.link.isEmpty())
                part += QStringLiteral(" ->") + block.link.toString();
            parts.append(part);
        }
    }
    return parts.join(QStringLiteral(" | "));
}

}  // namespace

class TestArticleHtml : public QObject
{
    Q_OBJECT

private slots:
    void blocks_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("expected");

        QTest::newRow("text only") << "<p>Hello <a href=\"https://x.org/\">link</a></p>"
                                   << "T<p>Hello <a href=\"https://x.org/\">link</a></p>";
        QTest::newRow("image between text") << "<p>a</p><img src=\"https://x.org/a.png\"><p>b</p>"
                                            << "T<p>a</p> | I:https://x.org/a.png | T<p>b</p>";
        QTest::newRow("image only") << "<img src=\"https://x.org/a.png\" alt=\"A\">" << "I:https://x.org/a.png";
        QTest::newRow("relative image") << "<img src=\"../img/a.png\">" << "I:https://news.example.org/img/a.png";
        QTest::newRow("protocol-relative") << "<img src=\"//cdn.example.org/a.png\">"
                                           << "I:https://cdn.example.org/a.png";
        QTest::newRow("entities in address") << "<img src=\"https://x.org/a.png?w=1&amp;h=2\">"
                                             << "I:https://x.org/a.png?w=1&h=2";
        QTest::newRow("lazy loading") << "<img src=\"data:image/gif;base64,R0lG\" data-src=\"https://x.org/real.png\">"
                                      << "I:https://x.org/real.png";
        QTest::newRow("srcset only") << "<img srcset=\"https://x.org/a-1x.png 1x, https://x.org/a-2x.png 2x\">"
                                     << "I:https://x.org/a-1x.png";
        QTest::newRow("tracking pixel") << "<p>a</p><img src=\"https://t.example/p.gif\" width=\"1\" height=\"1\">"
                                        << "T<p>a</p>";
        QTest::newRow("small icon kept") << "<img src=\"https://x.org/i.png\" width=\"16\" height=\"16\">"
                                         << "I:https://x.org/i.png";
        QTest::newRow("linked image") << "<a href=\"/full.jpg\"><img src=\"/thumb.jpg\"></a>"
                                      << "T<a href=\"https://news.example.org/full.jpg\"> | "
                                         "I:https://news.example.org/thumb.jpg ->https://news.example.org/full.jpg | T</a>";
        QTest::newRow("closed link before image") << "<a href=\"https://x.org/\">x</a><img src=\"https://x.org/a.png\">"
                                                  << "T<a href=\"https://x.org/\">x</a> | I:https://x.org/a.png";
        QTest::newRow("picture") << "<picture><source srcset=\"a.webp\"><img src=\"a.png\"></picture>"
                                 << "I:https://news.example.org/2026/a.png";
        QTest::newRow("relative link") << "<p><a href='more.html'>more</a></p>"
                                       << "T<p><a href=\"https://news.example.org/2026/more.html\">more</a></p>";
        QTest::newRow("script and style") << "<style>p{}</style><p>a</p><SCRIPT>\nx('<p>')\n</script >"
                                          << "T<p>a</p>";
        QTest::newRow("iframe becomes link")
            << "<p>a</p><iframe src=\"https://video.example/embed/1\" allowfullscreen></iframe>"
            << "T<p>a</p><p><a href=\"https://video.example/embed/1\">▶ Embedded content</a></p>";
        QTest::newRow("video removed") << "<video controls><source src=\"v.mp4\">No video</video><p>b</p>"
                                       << "T<p>b</p>";
        QTest::newRow("empty") << "" << "";
        QTest::newRow("whitespace between images") << "<img src=\"https://x.org/a.png\">\n <img src=\"https://x.org/b.png\">"
                                                   << "I:https://x.org/a.png | I:https://x.org/b.png";
    }

    void blocks()
    {
        QFETCH(QString, html);
        QFETCH(QString, expected);
        QCOMPARE(describe(split(html)), expected);
    }

    void noBaseUrl()
    {
        const QList<Block> list = ArticleHtml::blocks("<img src=\"https://x.org/a.png\">", QUrl(), "#fff");
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).image, QUrl("https://x.org/a.png"));
    }
};

QTEST_GUILESS_MAIN(TestArticleHtml)
#include "tst_articlehtml.moc"
