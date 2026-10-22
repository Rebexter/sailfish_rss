#ifndef SAILRSS_DATABASE_H
#define SAILRSS_DATABASE_H

#include "api/greadertypes.h"

#include <QSqlDatabase>
#include <QString>
#include <optional>

// Local SQLite store. The UI reads only from here; user actions are applied
// here first and queued as pending actions until a sync sends them.
//
// Methods returning bool or int report failure as false or -1, with the
// reason in lastError().
class Database
{
public:
    struct PendingAction {
        enum class Kind {
            Read,         // target: item ID, value: 1 read, 0 unread
            Starred,      // target: item ID, value: 1 starred, 0 not
            MarkAllRead,  // target: stream ID, value: older-than timestamp (µs)
        };
        qint64 seq = 0;  // actions must be sent in this order
        Kind kind = Kind::Read;
        QString target;
        qint64 value = 0;
    };

    struct StoredItem {
        QString id;
        QString feedId;
        QString title;
        QString author;
        QString url;
        QString contentHtml;
        QDateTime published;
        qint64 crawledUsec = 0;
        bool read = false;
        bool starred = false;
    };

    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens or creates the database file, creating its directory if needed.
    bool open(const QString &path);
    QString lastError() const;

    // --- Used by sync

    // Replaces the feed list; items of feeds that are gone are deleted.
    bool replaceSubscriptions(const QList<GReader::Subscription> &subscriptions);
    // Inserts new items and updates known ones. Read and starred state is
    // kept where a pending action has not been sent yet.
    bool upsertItems(const QList<GReader::Item> &items);
    // Sets read and starred state of all stored items from the server's
    // complete lists, again except where actions are pending.
    bool reconcileStates(const QStringList &unreadIds, const QStringList &starredIds);
    QStringList missingItemIds(const QStringList &ids);
    // Deletes read, unstarred items that were read before the cutoff (Unix
    // seconds). Returns the number deleted.
    int deleteExpired(qint64 readBeforeSecs);

    QList<PendingAction> pendingActions();
    // Removes sent actions. An action replaced by a newer one in the
    // meantime has a new seq and is kept.
    bool removePendingActions(const QList<qint64> &seqs);

    QString syncState(const QString &key);
    bool setSyncState(const QString &key, const QString &value);

    // --- User actions, applied locally and queued for the server

    bool setRead(const QString &itemId, bool read);
    bool setStarred(const QString &itemId, bool starred);
    // Marks the stream's items read that arrived up to olderThanUsec.
    bool markAllRead(const QString &streamId, qint64 olderThanUsec);

    // --- Queries

    // A folder or feed with its number of unread items.
    struct StreamSummary {
        QString id;
        QString title;
        QString iconUrl;
        int unread = 0;
    };

    // An item without its content, for lists.
    struct ItemSummary {
        QString id;
        QString feedTitle;
        QString title;
        QString author;
        QDateTime published;
        qint64 crawledUsec = 0;
        bool read = false;
        bool starred = false;
    };

    // Folders sorted by name, and the feeds of one folder.
    QList<StreamSummary> folders();
    QList<StreamSummary> feeds(const QString &folderId);
    enum class ItemFilter {
        All,
        Unread,
        Read,
    };

    // Items of a stream by arrival, newest or oldest first.
    QList<ItemSummary> items(const QString &streamId, ItemFilter filter, bool oldestFirst = false);

    std::optional<StoredItem> item(const QString &id);
    QString feedTitle(const QString &feedId);
    int itemCount();
    int feedCount();
    int starredCount();

    // Unread items that arrived after the given time, for notifications.
    struct NewItems {
        int count = 0;
        QString newestTitle;
    };
    NewItems newUnreadItems(qint64 arrivedAfterUsec);
    int unreadCount(const QString &streamId);

    // Deletes everything, e.g. when logging out.
    bool clear();

private:
    bool exec(const QString &sql);
    bool upgrade(int fromVersion);
    bool queuePendingAction(PendingAction::Kind kind, const QString &target, qint64 value);
    bool setFlag(const QString &column, PendingAction::Kind kind, const QString &itemId, bool value);
    int count(const QString &sql);

    QString m_connection;
    QSqlDatabase m_db;
    QString m_error;
};

#endif  // SAILRSS_DATABASE_H
