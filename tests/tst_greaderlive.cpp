// Tests against a real FreshRSS server. Development only: reads the server
// and credentials from the environment and skips if they are not set.
//   SAILRSS_URL, SAILRSS_USER, SAILRSS_PASSWORD (the API password)
//   SAILRSS_LIVE_WRITE=1 also runs tests that change read and starred
//   state; they restore the original state afterwards.
// Run with: scripts/dev.sh live-test
// TODO(phase 4): remove the environment variables together with the app's
// test-only credential path once credentials are stored in the keychain.

#include "api/greaderclient.h"

#include <QEventLoop>
#include <QSet>
#include <QTimer>
#include <QtTest>

using namespace GReader;

namespace {

// Runs the event loop until the callback passed to `start` is called.
template <typename Start>
void waitFor(Start start)
{
    QEventLoop loop;
    bool finished = false;
    start([&]() {
        finished = true;
        loop.quit();
    });
    if (!finished) {
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    QVERIFY2(finished, "Timed out waiting for the server");
}

QStringList ids(const QList<Item> &items)
{
    QStringList result;
    for (const Item &item : items)
        result.append(item.id);
    return result;
}

}  // namespace

class TestGReaderLive : public QObject
{
    Q_OBJECT

private:
    Client m_client;
    QString m_user;
    QString m_password;
    bool m_write = false;

    bool fetchStream(const StreamQuery &query, StreamPage *page)
    {
        Error error;
        waitFor([&](auto finish) {
            m_client.fetchStream(query, [&, finish](const Error &e, const StreamPage &p) {
                error = e;
                *page = p;
                finish();
            });
        });
        if (!error.ok())
            qWarning("fetchStream: %s", qPrintable(error.message));
        return error.ok();
    }

    bool newestItem(Item *item)
    {
        StreamQuery query;
        query.count = 1;
        StreamPage page;
        if (!fetchStream(query, &page))
            return false;
        *item = page.items.value(0);
        return true;
    }

    static QString env(const char *name) { return QString::fromLocal8Bit(qgetenv(name)); }

    Error call(const std::function<void(const Client::Callback &)> &request)
    {
        Error error;
        waitFor([&](auto finish) {
            request([&, finish](const Error &e) {
                error = e;
                finish();
            });
        });
        return error;
    }

private slots:
    void initTestCase()
    {
        const QString url = env("SAILRSS_URL");
        m_user = env("SAILRSS_USER");
        m_password = env("SAILRSS_PASSWORD");
        m_write = env("SAILRSS_LIVE_WRITE") == QLatin1String("1");
        if (url.isEmpty() || m_user.isEmpty() || m_password.isEmpty())
            QSKIP("Set SAILRSS_URL, SAILRSS_USER and SAILRSS_PASSWORD to run the live tests");

        m_client.setServerUrl(QUrl::fromUserInput(url));
        qInfo("API address: %s", qPrintable(m_client.apiUrl().toString()));

        const Error error = call([&](const Client::Callback &done) { m_client.login(m_user, m_password, done); });
        QVERIFY2(error.ok(), qPrintable(error.message));
        QVERIFY(!m_client.authToken().isEmpty());
    }

    void wrongPassword()
    {
        Client client;
        client.setServerUrl(m_client.apiUrl());
        const Error error = call([&](const Client::Callback &done) {
            client.login(m_user, m_password + QStringLiteral("-wrong"), done);
        });
        QCOMPARE(error.type, Error::Type::Auth);
        QVERIFY(client.authToken().isEmpty());
    }

    void subscriptions()
    {
        Error error;
        QList<Subscription> subscriptions;
        waitFor([&](auto finish) {
            m_client.fetchSubscriptions([&, finish](const Error &e, const QList<Subscription> &s) {
                error = e;
                subscriptions = s;
                finish();
            });
        });
        QVERIFY2(error.ok(), qPrintable(error.message));
        qInfo("%d subscriptions", subscriptions.size());
        for (const Subscription &subscription : subscriptions)
            QVERIFY(subscription.id.startsWith(QLatin1String("feed/")));
    }

    void tags()
    {
        Error error;
        QList<Tag> tags;
        waitFor([&](auto finish) {
            m_client.fetchTags([&, finish](const Error &e, const QList<Tag> &t) {
                error = e;
                tags = t;
                finish();
            });
        });
        QVERIFY2(error.ok(), qPrintable(error.message));
        qInfo("%d tags", tags.size());
        bool hasStarred = false;
        for (const Tag &tag : tags)
            hasStarred |= tag.id == StreamId::Starred;
        QVERIFY(hasStarred);
    }

    void unreadCounts()
    {
        Error error;
        QList<UnreadCount> counts;
        waitFor([&](auto finish) {
            m_client.fetchUnreadCounts([&, finish](const Error &e, const QList<UnreadCount> &c) {
                error = e;
                counts = c;
                finish();
            });
        });
        QVERIFY2(error.ok(), qPrintable(error.message));
        bool hasTotal = false;
        for (const UnreadCount &count : counts) {
            if (count.id == StreamId::ReadingList) {
                hasTotal = true;
                qInfo("%d unread in total", count.count);
            }
        }
        QVERIFY(hasTotal);
    }

    void streamPaging()
    {
        StreamQuery query;
        query.count = 2;
        StreamPage first;
        QVERIFY(fetchStream(query, &first));
        qInfo("first page: %d items", first.items.size());
        if (first.continuation.isEmpty())
            QSKIP("Fewer than 3 articles on the server, cannot test paging");

        query.continuation = first.continuation;
        StreamPage second;
        QVERIFY(fetchStream(query, &second));
        QVERIFY(!second.items.isEmpty());
        const QSet<QString> firstIds = ids(first.items).toSet();
        for (const QString &id : ids(second.items))
            QVERIFY2(!firstIds.contains(id), "Pages overlap");
    }

    void itemIdsMatchStream()
    {
        StreamQuery query;
        query.count = 5;
        StreamPage page;
        QVERIFY(fetchStream(query, &page));

        Error error;
        ItemIdsPage idsPage;
        waitFor([&](auto finish) {
            m_client.fetchItemIds(query, [&, finish](const Error &e, const ItemIdsPage &p) {
                error = e;
                idsPage = p;
                finish();
            });
        });
        QVERIFY2(error.ok(), qPrintable(error.message));
        // Both are newest first, and long and decimal IDs must agree.
        QCOMPARE(idsPage.ids, ids(page.items));
    }

    void readRoundTrip()
    {
        if (!m_write)
            QSKIP("Set SAILRSS_LIVE_WRITE=1 to test writes");
        Item item;
        QVERIFY(newestItem(&item));
        if (item.id.isEmpty())
            QSKIP("No articles on the server");

        Error error = call([&](const Client::Callback &done) { m_client.setRead({item.id}, !item.read, done); });
        QVERIFY2(error.ok(), qPrintable(error.message));
        Item changed;
        QVERIFY(newestItem(&changed));
        QCOMPARE(changed.id, item.id);
        QCOMPARE(changed.read, !item.read);

        error = call([&](const Client::Callback &done) { m_client.setRead({item.id}, item.read, done); });
        QVERIFY2(error.ok(), qPrintable(error.message));
        Item restored;
        QVERIFY(newestItem(&restored));
        QCOMPARE(restored.read, item.read);
    }

    void starRoundTrip()
    {
        if (!m_write)
            QSKIP("Set SAILRSS_LIVE_WRITE=1 to test writes");
        Item item;
        QVERIFY(newestItem(&item));
        if (item.id.isEmpty())
            QSKIP("No articles on the server");

        Error error = call([&](const Client::Callback &done) { m_client.setStarred({item.id}, !item.starred, done); });
        QVERIFY2(error.ok(), qPrintable(error.message));
        Item changed;
        QVERIFY(newestItem(&changed));
        QCOMPARE(changed.id, item.id);
        QCOMPARE(changed.starred, !item.starred);

        error = call([&](const Client::Callback &done) { m_client.setStarred({item.id}, item.starred, done); });
        QVERIFY2(error.ok(), qPrintable(error.message));
        Item restored;
        QVERIFY(newestItem(&restored));
        QCOMPARE(restored.starred, item.starred);
    }

    void markAllAsReadIsAccepted()
    {
        if (!m_write)
            QSKIP("Set SAILRSS_LIVE_WRITE=1 to test writes");
        // Nothing is older than 1 µs, so this changes nothing on the server.
        const Error error = call([&](const Client::Callback &done) {
            m_client.markAllAsRead(StreamId::ReadingList, 1, done);
        });
        QVERIFY2(error.ok(), qPrintable(error.message));
    }
};

QTEST_GUILESS_MAIN(TestGReaderLive)
#include "tst_greaderlive.moc"
