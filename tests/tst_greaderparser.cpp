#include "api/greaderclient.h"
#include "api/greaderparser.h"

#include <QFile>
#include <QtTest>

using namespace GReader;

class TestGReaderParser : public QObject
{
    Q_OBJECT

private:
    static QByteArray fixture(const QString &name)
    {
        QFile file(QFINDTESTDATA("data/" + name));
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Missing test data: %s", qPrintable(name));
        return file.readAll();
    }

private slots:
    void authToken()
    {
        QCOMPARE(Parser::parseAuthToken(fixture("login.txt")),
                 QStringLiteral("tester/0123456789abcdef0123456789abcdef01234567"));
        QCOMPARE(Parser::parseAuthToken("Unauthorized!"), QString());
    }

    void itemIdToDecimal_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("expected");
        QTest::newRow("long form") << "tag:google.com,2005:reader/item/00065e5e2dd76e18" << "1792608449293848";
        QTest::newRow("decimal") << "1792608449293848" << "1792608449293848";
        QTest::newRow("bad hex") << "tag:google.com,2005:reader/item/xyz" << "";
        QTest::newRow("garbage") << "not-an-id" << "";
        QTest::newRow("empty") << "" << "";
    }

    void itemIdToDecimal()
    {
        QFETCH(QString, id);
        QFETCH(QString, expected);
        QCOMPARE(Parser::itemIdToDecimal(id), expected);
    }

    void subscriptions()
    {
        const auto subscriptions = Parser::parseSubscriptions(fixture("subscriptions.json"));
        QVERIFY(subscriptions.has_value());
        QCOMPARE(subscriptions->size(), 2);

        const Subscription &first = subscriptions->at(0);
        QCOMPARE(first.id, QStringLiteral("feed/2"));
        QCOMPARE(first.title, QStringLiteral("Sailing News"));
        QCOMPARE(first.url, QStringLiteral("https://news.example.org/feed/"));
        QCOMPARE(first.htmlUrl, QStringLiteral("https://news.example.org/"));
        QCOMPARE(first.iconUrl, QStringLiteral("https://rss.example.net/f.php?h=efdffc6f"));
        QCOMPARE(first.categories.size(), 1);
        QCOMPARE(first.categories.at(0).id, QStringLiteral("user/-/label/Sailfish"));
        QCOMPARE(first.categories.at(0).label, QStringLiteral("Sailfish"));

        QCOMPARE(subscriptions->at(1).title, QStringLiteral("Café & Code"));
    }

    void tags()
    {
        const auto tags = Parser::parseTags(fixture("tags.json"));
        QVERIFY(tags.has_value());
        QCOMPARE(tags->size(), 4);
        QCOMPARE(tags->at(0).id, StreamId::Starred);
        QCOMPARE(tags->at(0).type, Tag::Type::State);
        QCOMPARE(tags->at(2).type, Tag::Type::Folder);
        QCOMPARE(tags->at(2).unreadCount, -1);
        QCOMPARE(tags->at(3).id, QStringLiteral("user/-/label/Read later"));
        QCOMPARE(tags->at(3).type, Tag::Type::Label);
        QCOMPARE(tags->at(3).unreadCount, 3);
    }

    void unreadCounts()
    {
        const auto counts = Parser::parseUnreadCounts(fixture("unread-count.json"));
        QVERIFY(counts.has_value());
        QCOMPARE(counts->size(), 3);
        QCOMPARE(counts->at(0).id, QStringLiteral("feed/2"));
        QCOMPARE(counts->at(0).count, 8);
        QCOMPARE(counts->at(0).newestItemUsec, Q_INT64_C(1792608449293849));
        QCOMPARE(counts->at(2).id, StreamId::ReadingList);
        QCOMPARE(counts->at(2).count, 18);
    }

    void stream()
    {
        const auto page = Parser::parseStream(fixture("stream-page.json"));
        QVERIFY(page.has_value());
        QCOMPARE(page->continuation, QStringLiteral("1792608449293847"));
        QCOMPARE(page->items.size(), 2);  // the item with an invalid ID is skipped

        const Item &first = page->items.at(0);
        QCOMPARE(first.id, QStringLiteral("1792608449293848"));
        QCOMPARE(first.title, QStringLiteral("Release 5.0 is here"));
        QCOMPARE(first.author, QStringLiteral("Jane Sailor"));
        QCOMPARE(first.url, QStringLiteral("https://news.example.org/release-5-0/"));
        QCOMPARE(first.contentHtml, QStringLiteral("<p><b>A new release</b> is out.</p>"));
        QCOMPARE(first.feedId, QStringLiteral("feed/2"));
        QCOMPARE(first.feedTitle, QStringLiteral("Sailing News"));
        QCOMPARE(first.published, QDateTime::fromMSecsSinceEpoch(Q_INT64_C(1740414037000), Qt::UTC));
        QCOMPARE(first.crawledUsec, Q_INT64_C(1792608449293848));
        QVERIFY(first.read);
        QVERIFY(first.starred);
        // The feed's own tags ("Community", "Releases") are not stream IDs.
        QCOMPARE(first.labels, QStringList({"user/-/label/Sailfish", "user/-/label/Read later"}));
        QCOMPARE(first.enclosures.size(), 1);  // the one without href is skipped
        QCOMPARE(first.enclosures.at(0).url, QStringLiteral("https://news.example.org/podcast.mp3"));
        QCOMPARE(first.enclosures.at(0).type, QStringLiteral("audio/mpeg"));
        QCOMPARE(first.enclosures.at(0).length, Q_INT64_C(123456));

        const Item &second = page->items.at(1);
        QVERIFY(!second.read);
        QVERIFY(!second.starred);
        QVERIFY(second.author.isEmpty());
        QCOMPARE(second.url, QStringLiteral("https://news.example.org/canonical-only/"));
        QCOMPARE(second.contentHtml, QStringLiteral("<p>Full content.</p>"));
    }

    void streamLastPage()
    {
        const auto page = Parser::parseStream(fixture("stream-last-page.json"));
        QVERIFY(page.has_value());
        QVERIFY(page->items.isEmpty());
        QVERIFY(page->continuation.isEmpty());
    }

    void itemIds()
    {
        const auto page = Parser::parseItemIds(fixture("item-ids.json"));
        QVERIFY(page.has_value());
        QCOMPARE(page->ids, QStringList({"1792608449293848", "1792608449293847", "1792608438046699"}));
        QCOMPARE(page->continuation, QStringLiteral("1792608438046699"));
    }

    void invalidResponses_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("error");
        QTest::newRow("html error page") << QByteArray("<html>Bad Gateway</html>") << "Invalid JSON";
        QTest::newRow("array") << QByteArray("[]") << "Expected a JSON object";
        QTest::newRow("wrong object") << QByteArray("{\"foo\": 1}") << "Missing array";
    }

    void invalidResponses()
    {
        QFETCH(QByteArray, body);
        QFETCH(QString, error);

        QString message;
        QVERIFY(!Parser::parseSubscriptions(body, &message));
        QVERIFY2(message.startsWith(error), qPrintable(message));
        QVERIFY(!Parser::parseTags(body));
        QVERIFY(!Parser::parseUnreadCounts(body));
        QVERIFY(!Parser::parseStream(body));
        QVERIFY(!Parser::parseItemIds(body));
    }

    void encodeForm()
    {
        const Params params = {{"Email", "user+tag@example.org"}, {"Passwd", "a&b=c d/é"}, {"i", "1"}, {"i", "2"}};
        QCOMPARE(GReader::encodeForm(params),
                 QByteArray("Email=user%2Btag%40example.org&Passwd=a%26b%3Dc%20d%2F%C3%A9&i=1&i=2"));
    }

    void encodedQuerySurvivesQUrl()
    {
        // Client puts encodeForm() output into QUrl; '+' in e.g. a "C++"
        // label must stay encoded, or PHP reads it as a space.
        QUrl url("https://rss.example.net/api/greader.php/reader/api/0/stream/contents");
        url.setQuery(QString::fromLatin1(GReader::encodeForm({{"s", "user/-/label/C++ & more"}})), QUrl::StrictMode);
        const QByteArray encoded = url.toEncoded();
        QVERIFY2(encoded.contains("C%2B%2B"), encoded.constData());
        QVERIFY2(encoded.contains("%26"), encoded.constData());
    }

    void apiUrlFromServerUrl_data()
    {
        QTest::addColumn<QString>("server");
        QTest::addColumn<QString>("api");
        const QString api = QStringLiteral("https://rss.example.net/api/greader.php");
        QTest::newRow("host") << "https://rss.example.net" << api;
        QTest::newRow("host slash") << "https://rss.example.net/" << api;
        QTest::newRow("web ui") << "https://rss.example.net/i/" << api;
        QTest::newRow("web ui query") << "https://rss.example.net/i/?c=feed&a=add" << api;
        QTest::newRow("api overview") << "https://rss.example.net/api/" << api;
        QTest::newRow("api address") << "https://rss.example.net/api/greader.php" << api;
        QTest::newRow("subdirectory") << "https://example.net/freshrss/i/"
                                      << "https://example.net/freshrss/api/greader.php";
        QTest::newRow("port") << "http://192.168.1.5:8080" << "http://192.168.1.5:8080/api/greader.php";
    }

    void apiUrlFromServerUrl()
    {
        QFETCH(QString, server);
        QFETCH(QString, api);
        QCOMPARE(Client::apiUrlFromServerUrl(QUrl(server)), QUrl(api));
    }
};

QTEST_GUILESS_MAIN(TestGReaderParser)
#include "tst_greaderparser.moc"
