#include "content/articlehtml.h"

#include <QtTest>

class TestArticleHtml : public QObject
{
    Q_OBJECT

private slots:
    void forRichText_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << "<p>Hello <a href=\"x\">link</a></p>" << "<p>Hello <a href=\"x\">link</a></p>";
        QTest::newRow("script") << "<p>a</p><script type=\"text/javascript\">alert('<p>')</script><p>b</p>"
                                << "<p>a</p><p>b</p>";
        QTest::newRow("style") << "<STYLE>p { color: red }</STYLE><p>a</p>" << "<p>a</p>";
        QTest::newRow("multiline") << "<p>a</p><script>\nvar x;\n</script >\n<p>b</p>" << "<p>a</p>\n<p>b</p>";
        QTest::newRow("image") << "<p><img src=\"https://x/a.png\" alt=\"A\" width=\"800\"/>text</p>"
                               << "<p>text</p>";
        QTest::newRow("picture") << "<picture><source srcset=\"a.webp\"><img src=\"a.png\"></picture><p>b</p>"
                                 << "<p>b</p>";
        QTest::newRow("iframe") << "<iframe src=\"https://video\" allowfullscreen></iframe><p>b</p>" << "<p>b</p>";
        QTest::newRow("figure caption kept") << "<figure><img src=\"a\"><figcaption>Cap</figcaption></figure>"
                                             << "<figure><figcaption>Cap</figcaption></figure>";
        QTest::newRow("image-like words") << "<p>imaging <imgur> stays</p>" << "<p>imaging <imgur> stays</p>";
    }

    void forRichText()
    {
        QFETCH(QString, html);
        QFETCH(QString, expected);
        const QString style = QStringLiteral("<style>a { color: #00ffff; }</style>");
        QCOMPARE(ArticleHtml::forRichText(html, "#00ffff"), style + expected);
    }
};

QTEST_GUILESS_MAIN(TestArticleHtml)
#include "tst_articlehtml.moc"
