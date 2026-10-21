#ifndef SAILRSS_SYNCMANAGER_H
#define SAILRSS_SYNCMANAGER_H

#include "api/greaderclient.h"
#include "storage/database.h"

#include <QObject>

// Synchronizes the local database with the server:
//  1. send pending actions (in order; stops if offline, nothing is lost)
//  2. replace the feed list
//  3. fetch articles that are new or changed since the last sync, at most
//     historyDays back
//  4. set read and starred state from the server's complete ID lists
//  5. fetch starred articles that are not stored yet, however old
//  6. delete read, unstarred articles read more than readRetentionDays ago
class SyncManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)

public:
    enum class ErrorKind {
        None,
        Network,  // offline or server unreachable; retry later
        Auth,     // credentials no longer valid
        Server,   // HTTP error or unexpected response
        Storage,  // local database error
    };

    struct Stats {
        int sent = 0;     // pending actions sent
        int fetched = 0;  // articles received
        int deleted = 0;  // expired articles removed
    };

    SyncManager(GReader::Client *client, Database *database, QObject *parent = nullptr);

    // How far back articles are fetched.
    void setHistoryDays(int days);
    // How long read, unstarred articles are kept after being read; 0 keeps
    // them forever.
    void setReadRetentionDays(int days);
    bool syncing() const;
    ErrorKind lastErrorKind() const;
    QString lastErrorMessage() const;
    Stats lastStats() const;

    // Both do nothing if a sync is already running.
    Q_INVOKABLE void sync();
    // Only step 1, to send user actions soon after they happen.
    Q_INVOKABLE void sendPendingActions();

signals:
    void syncingChanged();
    void finished(bool ok);

private:
    struct Batch {
        Database::PendingAction::Kind kind;
        qint64 value;
        QStringList targets;
        QList<qint64> seqs;
    };

    void start(bool sendOnly);
    void sendPending();
    void sendNextBatch();
    void fetchSubscriptions();
    void fetchArticles(const QString &continuation);
    void fetchIds(const GReader::StreamQuery &query, QStringList *ids, const std::function<void()> &next);
    void reconcile();
    void fetchMissingStarred(QStringList missing);
    void cleanUp();

    // Returns false and finishes the sync if there was an error.
    bool check(const GReader::Error &error);
    bool checkStorage(bool ok);
    void finish(ErrorKind kind, const QString &message);

    GReader::Client *m_client;
    Database *m_database;
    int m_historyDays = 600;
    int m_readRetentionDays = 30;
    bool m_syncing = false;
    ErrorKind m_errorKind = ErrorKind::None;
    QString m_errorMessage;
    Stats m_stats;

    // State of the running sync
    bool m_sendOnly = false;
    QList<Batch> m_batches;
    qint64 m_since = 0;
    qint64 m_serverTime = 0;
    QStringList m_unreadIds;
    QStringList m_starredIds;
};

#endif  // SAILRSS_SYNCMANAGER_H
