// Sync tests against the throwaway local FreshRSS started with
// scripts/dev.sh test-server. They change read and starred state freely,
// so never point them at a real account. scripts/dev.sh live-test sets
// SAILRSS_TEST_URL, SAILRSS_TEST_USER and SAILRSS_TEST_PASSWORD.

#include "api/greaderclient.h"
#include "storage/database.h"
#include "sync/syncmanager.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace GReader;

class TestSyncLive : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    Client m_client;
    Database m_db;
    int m_firstFetch = 0;

    static QString env(const char *name) { return QString::fromLocal8Bit(qgetenv(name)); }

    bool runSync(SyncManager *sync)
    {
        QSignalSpy finished(sync, &SyncManager::finished);
        sync->sync();
        if (!finished.wait(300000)) {
            qWarning("Sync did not finish in time");
            return false;
        }
        const SyncManager::Stats stats = sync->lastStats();
        qInfo("sync: ok=%d, %d sent, %d fetched, %d deleted, error: %s", finished.at(0).at(0).toBool(), stats.sent,
              stats.fetched, stats.deleted, qPrintable(sync->lastErrorMessage()));
        return finished.at(0).at(0).toBool();
    }

    // Read state of the newest article on the server.
    bool serverNewest(Item *item)
    {
        StreamQuery query;
        query.count = 1;
        Error error;
        QEventLoop loop;
        m_client.fetchStream(query, [&](const Error &e, const StreamPage &page) {
            error = e;
            *item = page.items.value(0);
            loop.quit();
        });
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        loop.exec();
        return error.ok() && !item->id.isEmpty();
    }

    template <typename T, typename Fetch>
    bool fetch(Fetch fetchFn, T *result)
    {
        Error error;
        QEventLoop loop;
        fetchFn([&](const Error &e, const T &value) {
            error = e;
            *result = value;
            loop.quit();
        });
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        loop.exec();
        return error.ok();
    }

private slots:
    void initTestCase()
    {
        const QString url = env("SAILRSS_TEST_URL");
        const QString user = env("SAILRSS_TEST_USER");
        const QString password = env("SAILRSS_TEST_PASSWORD");
        if (url.isEmpty() || user.isEmpty() || password.isEmpty())
            QSKIP("Run these with scripts/dev.sh live-test");

        QVERIFY(m_dir.isValid());
        QVERIFY2(m_db.open(m_dir.path() + QStringLiteral("/sailrss.sqlite")), qPrintable(m_db.lastError()));

        m_client.setServerUrl(QUrl::fromUserInput(url));
        Error error;
        QEventLoop loop;
        m_client.login(user, password, [&](const Error &e) {
            error = e;
            loop.quit();
        });
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY2(error.ok(), qPrintable(error.message));
    }

    void firstSyncFillsDatabase()
    {
        SyncManager sync(&m_client, &m_db);
        QVERIFY(runSync(&sync));
        m_firstFetch = sync.lastStats().fetched;

        QList<Subscription> subscriptions;
        QVERIFY(fetch<QList<Subscription>>(
            [&](const Client::ResultCallback<QList<Subscription>> &done) { m_client.fetchSubscriptions(done); },
            &subscriptions));
        QCOMPARE(m_db.feedCount(), subscriptions.size());
        qInfo("stored: %d feeds, %d articles, %d unread", m_db.feedCount(), m_db.itemCount(),
              m_db.unreadCount(StreamId::ReadingList));

        // Every starred article is stored, however old.
        ItemIdsPage starred;
        StreamQuery query;
        query.streamId = StreamId::Starred;
        query.count = 1000;
        QVERIFY(fetch<ItemIdsPage>(
            [&](const Client::ResultCallback<ItemIdsPage> &done) { m_client.fetchItemIds(query, done); }, &starred));
        QCOMPARE(m_db.missingItemIds(starred.ids), QStringList());
        QVERIFY(m_db.pendingActions().isEmpty());
    }

    void starredOutsideWindowAreFetched()
    {
        // With no history window, starred articles can only arrive through
        // the separate fetch of missing starred items.
        QTemporaryDir dir;
        Database db;
        QVERIFY2(db.open(dir.path() + QStringLiteral("/window.sqlite")), qPrintable(db.lastError()));
        SyncManager sync(&m_client, &db);
        sync.setHistoryDays(0);
        QVERIFY(runSync(&sync));

        qInfo("zero-day window stored %d articles", db.itemCount());
        ItemIdsPage ids;
        StreamQuery query;
        query.streamId = StreamId::Starred;
        query.count = 1000;
        QVERIFY(fetch<ItemIdsPage>(
            [&](const Client::ResultCallback<ItemIdsPage> &done) { m_client.fetchItemIds(query, done); }, &ids));
        if (ids.ids.isEmpty())
            QSKIP("No starred articles on the server");
        QCOMPARE(db.missingItemIds(ids.ids), QStringList());
        for (const QString &id : ids.ids)
            QVERIFY(db.item(id)->starred);
    }

    void secondSyncIsIncremental()
    {
        SyncManager sync(&m_client, &m_db);
        QVERIFY(runSync(&sync));
        QVERIFY(sync.lastStats().fetched <= m_firstFetch);
    }

    void offlineChangeIsSentLater()
    {
        Item newest;
        QVERIFY(serverNewest(&newest));
        QVERIFY(m_db.item(newest.id).has_value());
        const bool original = newest.read;

        // Change while offline: the sync fails, the change is kept.
        QVERIFY(m_db.setRead(newest.id, !original));
        Client offline;
        offline.setServerUrl(QUrl("http://127.0.0.1:9"));
        offline.setAuthToken(m_client.authToken());
        SyncManager offlineSync(&offline, &m_db);
        QVERIFY(!runSync(&offlineSync));
        QCOMPARE(offlineSync.lastErrorKind(), SyncManager::ErrorKind::Network);
        QCOMPARE(m_db.pendingActions().size(), 1);
        QCOMPARE(m_db.item(newest.id)->read, !original);

        // Back online: the change reaches the server and survives the pull.
        SyncManager sync(&m_client, &m_db);
        QVERIFY(runSync(&sync));
        QCOMPARE(sync.lastStats().sent, 1);
        QVERIFY(m_db.pendingActions().isEmpty());
        QCOMPARE(m_db.item(newest.id)->read, !original);
        Item onServer;
        QVERIFY(serverNewest(&onServer));
        QCOMPARE(onServer.read, !original);

        // Restore the original state.
        QVERIFY(m_db.setRead(newest.id, original));
        QVERIFY(runSync(&sync));
        QVERIFY(serverNewest(&onServer));
        QCOMPARE(onServer.read, original);
    }
};

QTEST_GUILESS_MAIN(TestSyncLive)
#include "tst_synclive.moc"
