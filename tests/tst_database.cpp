#include "storage/database.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace GReader;
using Kind = Database::PendingAction::Kind;

namespace {

Subscription feed(const QString &id, const QString &category)
{
    Subscription subscription;
    subscription.id = id;
    subscription.title = id + QStringLiteral(" title");
    subscription.categories.append({StreamId::LabelPrefix + category, category});
    return subscription;
}

Item item(qint64 id, const QString &feedId, bool read = false, bool starred = false)
{
    Item item;
    item.id = QString::number(id);
    item.feedId = feedId;
    item.title = QStringLiteral("Item %1").arg(id);
    item.crawledUsec = id;
    item.published = QDateTime::fromMSecsSinceEpoch(Q_INT64_C(1740414037000), Qt::UTC);
    item.read = read;
    item.starred = starred;
    return item;
}

}  // namespace

class TestDatabase : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Database> m_db;

    QString path() const { return m_dir->path() + QStringLiteral("/sub/sailrss.sqlite"); }

    static qint64 now() { return QDateTime::currentMSecsSinceEpoch() / 1000; }

    // Runs SQL on the database file through a separate connection and
    // returns the first value of the result, if any.
    QVariant raw(const QString &sql)
    {
        QVariant result;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "raw");
            db.setDatabaseName(path());
            if (db.open()) {
                QSqlQuery query(db);
                if (!query.exec(sql))
                    qWarning("raw SQL failed: %s", qPrintable(sql));
                else if (query.next())
                    result = query.value(0);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase("raw");
        return result;
    }

    QVariant readAt(qint64 id) { return raw(QStringLiteral("SELECT read_at FROM items WHERE id = %1").arg(id)); }

    bool read(qint64 id) { return m_db->item(QString::number(id))->read; }
    bool starred(qint64 id) { return m_db->item(QString::number(id))->starred; }

    // Two feeds in two categories, items 100-103 in feed/1 and 200-201 in feed/2.
    void fill()
    {
        QVERIFY2(m_db->replaceSubscriptions({feed("feed/1", "News"), feed("feed/2", "Tech")}),
                 qPrintable(m_db->lastError()));
        QVERIFY2(m_db->upsertItems({item(100, "feed/1"), item(101, "feed/1"), item(102, "feed/1", true),
                                    item(103, "feed/1", false, true), item(200, "feed/2"), item(201, "feed/2")}),
                 qPrintable(m_db->lastError()));
    }

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_db.reset(new Database);
        QVERIFY2(m_db->open(path()), qPrintable(m_db->lastError()));
    }

    void cleanup()
    {
        m_db.reset();
        m_dir.reset();
    }

    void persistsAcrossReopen()
    {
        fill();
        QVERIFY(m_db->setSyncState("lastSyncTime", "1792608469"));
        m_db.reset(new Database);
        QVERIFY2(m_db->open(path()), qPrintable(m_db->lastError()));
        QCOMPARE(m_db->syncState("lastSyncTime"), QStringLiteral("1792608469"));
        QCOMPARE(m_db->itemCount(), 6);
        QCOMPARE(m_db->feedCount(), 2);
    }

    void refusesNewerSchema()
    {
        m_db.reset();
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "raw");
            raw.setDatabaseName(path());
            QVERIFY(raw.open());
            QVERIFY(QSqlQuery(raw).exec("PRAGMA user_version = 99"));
            raw.close();
        }
        QSqlDatabase::removeDatabase("raw");

        m_db.reset(new Database);
        QVERIFY(!m_db->open(path()));
        QVERIFY2(m_db->lastError().contains("newer version"), qPrintable(m_db->lastError()));
    }

    void storesItemFields()
    {
        fill();
        Item full = item(300, "feed/1", true, true);
        full.author = "Jane Sailor";
        full.url = "https://news.example.org/a/";
        full.contentHtml = "<p>Hi</p>";
        QVERIFY(m_db->upsertItems({full}));

        const auto stored = m_db->item("300");
        QVERIFY(stored.has_value());
        QCOMPARE(stored->feedId, QStringLiteral("feed/1"));
        QCOMPARE(stored->title, QStringLiteral("Item 300"));
        QCOMPARE(stored->author, QStringLiteral("Jane Sailor"));
        QCOMPARE(stored->url, QStringLiteral("https://news.example.org/a/"));
        QCOMPARE(stored->contentHtml, QStringLiteral("<p>Hi</p>"));
        QCOMPARE(stored->published, full.published);
        QCOMPARE(stored->crawledUsec, Q_INT64_C(300));
        QVERIFY(stored->read);
        QVERIFY(stored->starred);
        QVERIFY(!m_db->item("999").has_value());
    }

    void upsertUpdatesKnownItems()
    {
        fill();
        Item changed = item(100, "feed/1", true);
        changed.title = "New title";
        QVERIFY(m_db->upsertItems({changed}));
        QCOMPARE(m_db->itemCount(), 6);
        QCOMPARE(m_db->item("100")->title, QStringLiteral("New title"));
        QVERIFY(read(100));
    }

    void removedFeedsLoseTheirItems()
    {
        fill();
        QVERIFY(m_db->replaceSubscriptions({feed("feed/1", "News")}));
        QCOMPARE(m_db->feedCount(), 1);
        QCOMPARE(m_db->itemCount(), 4);
        QVERIFY(!m_db->item("200").has_value());
    }

    void userActionsQueueOnlyRealChanges()
    {
        fill();
        QVERIFY(m_db->setRead("102", true));  // already read
        QVERIFY(m_db->setRead("999", true));  // unknown
        QVERIFY(m_db->pendingActions().isEmpty());

        QVERIFY(m_db->setRead("100", true));
        QVERIFY(m_db->setStarred("101", true));
        const auto actions = m_db->pendingActions();
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.at(0).kind, Kind::Read);
        QCOMPARE(actions.at(0).target, QStringLiteral("100"));
        QCOMPARE(actions.at(0).value, Q_INT64_C(1));
        QCOMPARE(actions.at(1).kind, Kind::Starred);
        QVERIFY(read(100));
        QVERIFY(starred(101));
    }

    void newerActionReplacesOlder()
    {
        fill();
        QVERIFY(m_db->setRead("100", true));
        QVERIFY(m_db->setStarred("101", true));
        const qint64 firstSeq = m_db->pendingActions().at(0).seq;
        QVERIFY(m_db->setRead("100", false));

        const auto actions = m_db->pendingActions();
        QCOMPARE(actions.size(), 2);
        // The read action moved behind the star action, with the new value.
        QCOMPARE(actions.at(1).kind, Kind::Read);
        QCOMPARE(actions.at(1).value, Q_INT64_C(0));
        QVERIFY(actions.at(1).seq != firstSeq);

        // Removing the sent (old) seq keeps the newer action.
        QVERIFY(m_db->removePendingActions({firstSeq}));
        QCOMPARE(m_db->pendingActions().size(), 2);
        QVERIFY(m_db->removePendingActions({actions.at(0).seq, actions.at(1).seq}));
        QVERIFY(m_db->pendingActions().isEmpty());
    }

    void upsertKeepsPendingState()
    {
        fill();
        QVERIFY(m_db->setRead("100", true));
        QVERIFY(m_db->setStarred("100", true));
        QVERIFY(m_db->upsertItems({item(100, "feed/1", false, false), item(101, "feed/1", true, true)}));
        QVERIFY(read(100));
        QVERIFY(starred(100));
        // Without pending actions the server wins.
        QVERIFY(read(101));
        QVERIFY(starred(101));
    }

    void reconcileAppliesServerState()
    {
        fill();
        QVERIFY(m_db->reconcileStates({"102", "201"}, {"100"}));
        QCOMPARE(m_db->unreadCount(StreamId::ReadingList), 2);
        QVERIFY(read(100));
        QVERIFY(!read(102));
        QVERIFY(!read(201));
        QVERIFY(starred(100));
        QVERIFY(!starred(103));
    }

    void reconcileSkipsPendingItems()
    {
        fill();
        QVERIFY(m_db->setRead("100", true));
        QVERIFY(m_db->setStarred("103", false));
        QVERIFY(m_db->reconcileStates({"100", "101"}, {"103"}));
        QVERIFY(read(100));
        QVERIFY(!starred(103));
        QVERIFY(!read(101));
        QVERIFY(read(200));
    }

    void reconcileSkipsReadWhileMarkAllPending()
    {
        fill();
        QVERIFY(m_db->markAllRead(StreamId::ReadingList, 1000));
        QVERIFY(m_db->reconcileStates({"100", "101"}, {"101"}));
        QCOMPARE(m_db->unreadCount(StreamId::ReadingList), 0);
        QVERIFY(starred(101));  // starred state is still reconciled
    }

    void markAllReadPerStream()
    {
        fill();
        QCOMPARE(m_db->unreadCount(StreamId::ReadingList), 5);
        QCOMPARE(m_db->unreadCount("feed/1"), 3);
        QCOMPARE(m_db->unreadCount("user/-/label/Tech"), 2);
        QCOMPARE(m_db->unreadCount(StreamId::Starred), 1);

        // Only items that arrived up to the given time are marked.
        QVERIFY(m_db->markAllRead("feed/1", 100));
        QCOMPARE(m_db->unreadCount("feed/1"), 2);
        QVERIFY(m_db->markAllRead("user/-/label/Tech", 201));
        QCOMPARE(m_db->unreadCount("feed/2"), 0);
        QCOMPARE(m_db->unreadCount(StreamId::ReadingList), 2);

        const auto actions = m_db->pendingActions();
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.at(0).kind, Kind::MarkAllRead);
        QCOMPARE(actions.at(0).target, QStringLiteral("feed/1"));
        QCOMPARE(actions.at(0).value, Q_INT64_C(100));
        QCOMPARE(actions.at(1).target, QStringLiteral("user/-/label/Tech"));

        QVERIFY(!m_db->markAllRead("user/-/state/org.freshrss/important", 1000));
        QCOMPARE(m_db->unreadCount("bogus"), -1);
    }

    void listsFoldersAndFeeds()
    {
        fill();
        QVERIFY(m_db->replaceSubscriptions({feed("feed/1", "News"), feed("feed/2", "Tech"), feed("feed/3", "news2"),
                                            feed("feed/4", "Tech")}));
        QVERIFY(m_db->setRead("200", true));

        const auto folders = m_db->folders();
        QCOMPARE(folders.size(), 3);  // sorted case-insensitively
        QCOMPARE(folders.at(0).title, QStringLiteral("News"));
        QCOMPARE(folders.at(0).id, QStringLiteral("user/-/label/News"));
        QCOMPARE(folders.at(0).unread, 3);
        QCOMPARE(folders.at(1).title, QStringLiteral("news2"));
        QCOMPARE(folders.at(1).unread, 0);  // a feed without items
        QCOMPARE(folders.at(2).title, QStringLiteral("Tech"));
        QCOMPARE(folders.at(2).unread, 1);

        const auto feeds = m_db->feeds("user/-/label/Tech");
        QCOMPARE(feeds.size(), 2);
        QCOMPARE(feeds.at(0).id, QStringLiteral("feed/2"));
        QCOMPARE(feeds.at(0).title, QStringLiteral("feed/2 title"));
        QCOMPARE(feeds.at(0).unread, 1);
        QCOMPARE(feeds.at(1).unread, 0);
        QCOMPARE(m_db->feedTitle("feed/4"), QStringLiteral("feed/4 title"));
    }

    void listsItemsNewestFirst()
    {
        fill();
        using Filter = Database::ItemFilter;
        const auto all = m_db->items("feed/1", Filter::All);
        QCOMPARE(all.size(), 4);
        QCOMPARE(all.at(0).id, QStringLiteral("103"));
        QCOMPARE(all.at(0).feedTitle, QStringLiteral("feed/1 title"));
        QVERIFY(all.at(0).starred);
        QCOMPARE(all.at(3).id, QStringLiteral("100"));

        const auto unread = m_db->items("feed/1", Filter::Unread);
        QCOMPARE(unread.size(), 3);  // 102 is read
        const auto read = m_db->items("feed/1", Filter::Read);
        QCOMPARE(read.size(), 1);
        QCOMPARE(read.at(0).id, QStringLiteral("102"));
        QCOMPARE(m_db->items("user/-/label/Tech", Filter::All).size(), 2);
        QCOMPARE(m_db->items(StreamId::ReadingList, Filter::Unread).size(), 5);
        QCOMPARE(m_db->items(StreamId::ReadingList, Filter::Read).size(), 1);
        QCOMPARE(m_db->items(StreamId::Starred, Filter::All).size(), 1);

        const auto oldest = m_db->items("feed/1", Filter::All, true);
        QCOMPARE(oldest.first().id, QStringLiteral("100"));
        QCOMPARE(oldest.last().id, QStringLiteral("103"));
        QCOMPARE(m_db->starredCount(), 1);
    }

    void clearRemovesEverything()
    {
        fill();
        QVERIFY(m_db->setRead("100", true));
        QVERIFY(m_db->setSyncState("lastSyncTime", "1"));
        QVERIFY(m_db->clear());
        QCOMPARE(m_db->itemCount(), 0);
        QCOMPARE(m_db->feedCount(), 0);
        QVERIFY(m_db->pendingActions().isEmpty());
        QVERIFY(m_db->syncState("lastSyncTime").isEmpty());
    }

    void newUnreadItems()
    {
        fill();  // unread: 100, 101, 103, 200, 201
        const auto after101 = m_db->newUnreadItems(101);
        QCOMPARE(after101.count, 3);  // 103, 200, 201
        QCOMPARE(after101.newestTitle, QStringLiteral("Item 201"));
        QVERIFY(m_db->setRead("201", true));
        QCOMPARE(m_db->newUnreadItems(101).newestTitle, QStringLiteral("Item 200"));
        QCOMPARE(m_db->newUnreadItems(1000).count, 0);
        QVERIFY(m_db->newUnreadItems(1000).newestTitle.isEmpty());
    }

    void missingItemIds()
    {
        fill();
        QCOMPARE(m_db->missingItemIds({"100", "555", "201", "556"}), QStringList({"555", "556"}));
    }

    void readTimeFollowsReadState()
    {
        fill();
        QVERIFY(readAt(100).isNull());                     // unread
        QCOMPARE(readAt(102).toLongLong(), Q_INT64_C(0));  // first seen read: its arrival (102 µs)
        QVERIFY(m_db->setRead("100", true));
        QVERIFY(qAbs(readAt(100).toLongLong() - now()) <= 5);
        QVERIFY(m_db->setRead("100", false));
        QVERIFY(readAt(100).isNull());

        // From the server, by sync or by mark all as read
        QVERIFY(m_db->reconcileStates({"100", "102", "103", "200", "201"}, {}));
        QVERIFY(!readAt(101).isNull());
        QVERIFY(readAt(102).isNull());  // unread again
        QVERIFY(m_db->markAllRead("feed/2", 201));
        QVERIFY(!readAt(200).isNull());

        // Staying read keeps the original time.
        QVERIFY(raw("UPDATE items SET read_at = 1000 WHERE id = 101").isNull());
        QVERIFY(m_db->upsertItems({item(101, "feed/1", true)}));
        QCOMPARE(readAt(101).toLongLong(), Q_INT64_C(1000));
    }

    void deleteExpiredGoesByReadTime()
    {
        fill();
        QVERIFY(m_db->setRead("100", true));  // read, but not yet sent
        QVERIFY(m_db->reconcileStates({"100", "101"}, {"103"}));
        // Now: 100 read (pending), 101 unread, 102 read, 103 read+starred,
        // 200 and 201 read. All but 201 were read long ago.
        QVERIFY(raw("UPDATE items SET read_at = 1000 WHERE id != 201").isNull());
        QCOMPARE(m_db->deleteExpired(now() - 3600), 2);  // 102 and 200
        QVERIFY(m_db->item("100").has_value());
        QVERIFY(m_db->item("101").has_value());
        QVERIFY(!m_db->item("102").has_value());
        QVERIFY(m_db->item("103").has_value());
        QVERIFY(!m_db->item("200").has_value());
        QVERIFY(m_db->item("201").has_value());  // read recently
    }

    void readArticlesFromServerCountFromArrival()
    {
        fill();
        Item old = item(300, "feed/1", true);
        old.crawledUsec = (now() - 90 * 24 * 3600) * Q_INT64_C(1000000);  // 90 days ago
        QVERIFY(m_db->upsertItems({old}));
        QCOMPARE(readAt(300).toLongLong(), old.crawledUsec / 1000000);
        // So a 30-day retention deletes it at once, like 102 (read from the
        // start, arrived in 1970).
        QCOMPARE(m_db->deleteExpired(now() - 30 * 24 * 3600), 2);
        QVERIFY(!m_db->item("300").has_value());
        QVERIFY(!m_db->item("102").has_value());
        QVERIFY(m_db->item("100").has_value());  // unread
    }

    void upgradesVersion1()
    {
        m_db.reset();
        QFile::remove(path());
        // The version 1 tables used here, with one read and one unread item
        raw("CREATE TABLE items (id INTEGER PRIMARY KEY, feed_id TEXT NOT NULL, title TEXT, author TEXT, url TEXT, "
            "content TEXT, published INTEGER, crawled_usec INTEGER NOT NULL, read INTEGER NOT NULL DEFAULT 0, "
            "starred INTEGER NOT NULL DEFAULT 0)");
        raw("INSERT INTO items (id, feed_id, crawled_usec, read) VALUES (1, 'feed/1', 1, 1), (2, 'feed/1', 2, 0)");
        raw("CREATE TABLE pending_actions (seq INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, "
            "target TEXT NOT NULL, value INTEGER NOT NULL, UNIQUE (kind, target))");
        raw("PRAGMA user_version = 1");

        m_db.reset(new Database);
        QVERIFY2(m_db->open(path()), qPrintable(m_db->lastError()));
        QCOMPARE(raw("PRAGMA user_version").toInt(), 3);
        // Already read: counted from the upgrade, so nothing is deleted at once.
        QVERIFY(qAbs(readAt(1).toLongLong() - now()) <= 5);
        QVERIFY(readAt(2).isNull());
        QCOMPARE(m_db->deleteExpired(now() - 3600), 0);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
