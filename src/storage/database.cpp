#include "database.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace StreamId = GReader::StreamId;
using Kind = Database::PendingAction::Kind;

namespace {

const int SchemaVersion = 1;

const char *const Schema[] = {
    "CREATE TABLE feeds ("
    "  id TEXT PRIMARY KEY,"
    "  title TEXT NOT NULL,"
    "  url TEXT,"
    "  html_url TEXT,"
    "  icon_url TEXT,"
    "  category_id TEXT,"
    "  category_label TEXT)",
    // id is FreshRSS's decimal entry ID, which is also its arrival time in µs.
    "CREATE TABLE items ("
    "  id INTEGER PRIMARY KEY,"
    "  feed_id TEXT NOT NULL,"
    "  title TEXT,"
    "  author TEXT,"
    "  url TEXT,"
    "  content TEXT,"
    "  published INTEGER,"
    "  crawled_usec INTEGER NOT NULL,"
    "  read INTEGER NOT NULL DEFAULT 0,"
    "  starred INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX items_by_feed ON items (feed_id, crawled_usec)",
    "CREATE INDEX items_by_state ON items (read, feed_id)",
    // One row per (kind, target): a newer action replaces the older one and
    // gets a new seq, so the queue holds only the latest intent.
    "CREATE TABLE pending_actions ("
    "  seq INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  kind TEXT NOT NULL,"
    "  target TEXT NOT NULL,"
    "  value INTEGER NOT NULL,"
    "  UNIQUE (kind, target))",
    "CREATE TABLE sync_state ("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT)",
};

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Read:
        return QStringLiteral("read");
    case Kind::Starred:
        return QStringLiteral("starred");
    case Kind::MarkAllRead:
        return QStringLiteral("mark_all_read");
    }
    return QString();
}

std::optional<Kind> kindFromName(const QString &name)
{
    for (Kind kind : {Kind::Read, Kind::Starred, Kind::MarkAllRead}) {
        if (kindName(kind) == name)
            return kind;
    }
    return std::nullopt;
}

// SQL condition selecting the items of a stream. Conditions that need the
// stream ID use the placeholder :stream.
std::optional<QString> streamCondition(const QString &streamId)
{
    if (streamId == StreamId::ReadingList)
        return QStringLiteral("1");
    if (streamId == StreamId::Starred)
        return QStringLiteral("starred = 1");
    if (streamId.startsWith(QLatin1String("feed/")))
        return QStringLiteral("feed_id = :stream");
    if (streamId.startsWith(StreamId::LabelPrefix))
        return QStringLiteral("feed_id IN (SELECT id FROM feeds WHERE category_id = :stream)");
    return std::nullopt;
}

// Rolls back unless committed.
class Transaction
{
public:
    explicit Transaction(QSqlDatabase &db)
        : m_db(db)
        , m_active(db.transaction())
    {
    }
    ~Transaction()
    {
        if (m_active)
            m_db.rollback();
    }
    bool active() const { return m_active; }
    bool commit()
    {
        m_active = false;
        return m_db.commit();
    }

private:
    QSqlDatabase &m_db;
    bool m_active;
};

}  // namespace

Database::Database()
{
    static int instances = 0;
    m_connection = QStringLiteral("sailrss-%1").arg(++instances);
}

Database::~Database()
{
    if (m_db.isValid())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connection))
        QSqlDatabase::removeDatabase(m_connection);
}

bool Database::open(const QString &path)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_error = QStringLiteral("Cannot create the directory for %1").arg(path);
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    if (!exec(QStringLiteral("PRAGMA journal_mode = WAL")))
        return false;

    const int version = count(QStringLiteral("PRAGMA user_version"));
    if (version < 0)
        return false;
    if (version == 0)
        return createSchema();
    if (version > SchemaVersion) {
        m_error = QStringLiteral("The database was created by a newer version of SailRSS");
        return false;
    }
    return true;
}

QString Database::lastError() const
{
    return m_error;
}

bool Database::createSchema()
{
    Transaction transaction(m_db);
    for (const char *statement : Schema) {
        if (!exec(QString::fromLatin1(statement)))
            return false;
    }
    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion)))
        return false;
    return transaction.commit();
}

bool Database::replaceSubscriptions(const QList<GReader::Subscription> &subscriptions)
{
    Transaction transaction(m_db);
    if (!exec(QStringLiteral("DELETE FROM feeds")))
        return false;

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral("INSERT INTO feeds (id, title, url, html_url, icon_url, category_id, category_label) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    for (const GReader::Subscription &subscription : subscriptions) {
        // FreshRSS puts every feed in exactly one category.
        const GReader::Category category = subscription.categories.value(0);
        insert.addBindValue(subscription.id);
        insert.addBindValue(subscription.title);
        insert.addBindValue(subscription.url);
        insert.addBindValue(subscription.htmlUrl);
        insert.addBindValue(subscription.iconUrl);
        insert.addBindValue(category.id);
        insert.addBindValue(category.label);
        if (!insert.exec()) {
            m_error = insert.lastError().text();
            return false;
        }
    }

    if (!exec(QStringLiteral("DELETE FROM items WHERE feed_id NOT IN (SELECT id FROM feeds)")))
        return false;
    return transaction.commit();
}

bool Database::upsertItems(const QList<GReader::Item> &items)
{
    Transaction transaction(m_db);
    QSqlQuery upsert(m_db);
    upsert.prepare(QStringLiteral(
        "INSERT INTO items (id, feed_id, title, author, url, content, published, crawled_usec, read, starred) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT (id) DO UPDATE SET "
        "  feed_id = excluded.feed_id, title = excluded.title, author = excluded.author,"
        "  url = excluded.url, content = excluded.content, published = excluded.published,"
        "  crawled_usec = excluded.crawled_usec,"
        "  read = CASE WHEN EXISTS (SELECT 1 FROM pending_actions"
        "    WHERE kind = 'read' AND target = CAST(items.id AS TEXT))"
        "    THEN items.read ELSE excluded.read END,"
        "  starred = CASE WHEN EXISTS (SELECT 1 FROM pending_actions"
        "    WHERE kind = 'starred' AND target = CAST(items.id AS TEXT))"
        "    THEN items.starred ELSE excluded.starred END"));
    for (const GReader::Item &item : items) {
        upsert.addBindValue(item.id.toLongLong());
        upsert.addBindValue(item.feedId);
        upsert.addBindValue(item.title);
        upsert.addBindValue(item.author);
        upsert.addBindValue(item.url);
        upsert.addBindValue(item.contentHtml);
        upsert.addBindValue(item.published.isValid() ? QVariant(item.published.toMSecsSinceEpoch() / 1000)
                                                     : QVariant(QVariant::LongLong));
        upsert.addBindValue(item.crawledUsec);
        upsert.addBindValue(item.read ? 1 : 0);
        upsert.addBindValue(item.starred ? 1 : 0);
        if (!upsert.exec()) {
            m_error = upsert.lastError().text();
            return false;
        }
    }
    return transaction.commit();
}

bool Database::reconcileStates(const QStringList &unreadIds, const QStringList &starredIds)
{
    Transaction transaction(m_db);
    if (!exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS sync_ids ("
                             "kind TEXT NOT NULL, id INTEGER NOT NULL, PRIMARY KEY (kind, id))"))
        || !exec(QStringLiteral("DELETE FROM temp.sync_ids")))
        return false;

    QVariantList kinds;
    QVariantList ids;
    for (const QString &id : unreadIds) {
        kinds << QStringLiteral("unread");
        ids << id.toLongLong();
    }
    for (const QString &id : starredIds) {
        kinds << QStringLiteral("starred");
        ids << id.toLongLong();
    }
    if (!ids.isEmpty()) {
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("INSERT OR IGNORE INTO temp.sync_ids (kind, id) VALUES (?, ?)"));
        insert.addBindValue(kinds);
        insert.addBindValue(ids);
        if (!insert.execBatch()) {
            m_error = insert.lastError().text();
            return false;
        }
    }

    // A pending mark-all-read covers items without their own pending row,
    // so read state is left alone entirely until it has been sent.
    const bool ok = exec(QStringLiteral(
                        "UPDATE items SET read = (id NOT IN (SELECT id FROM temp.sync_ids WHERE kind = 'unread')) "
                        "WHERE CAST(id AS TEXT) NOT IN (SELECT target FROM pending_actions WHERE kind = 'read') "
                        "AND NOT EXISTS (SELECT 1 FROM pending_actions WHERE kind = 'mark_all_read')"))
        && exec(QStringLiteral(
            "UPDATE items SET starred = (id IN (SELECT id FROM temp.sync_ids WHERE kind = 'starred')) "
            "WHERE CAST(id AS TEXT) NOT IN (SELECT target FROM pending_actions WHERE kind = 'starred')"));
    if (!ok)
        return false;
    return transaction.commit();
}

QStringList Database::missingItemIds(const QStringList &ids)
{
    QStringList missing;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT 1 FROM items WHERE id = ?"));
    for (const QString &id : ids) {
        query.addBindValue(id.toLongLong());
        if (!query.exec()) {
            m_error = query.lastError().text();
            return QStringList();
        }
        if (!query.next())
            missing.append(id);
    }
    return missing;
}

int Database::deleteExpired(qint64 cutoffUsec)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM items WHERE crawled_usec < ? AND read = 1 AND starred = 0 "
        "AND CAST(id AS TEXT) NOT IN (SELECT target FROM pending_actions WHERE kind IN ('read', 'starred'))"));
    query.addBindValue(cutoffUsec);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return -1;
    }
    return query.numRowsAffected();
}

QList<Database::PendingAction> Database::pendingActions()
{
    QList<PendingAction> actions;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT seq, kind, target, value FROM pending_actions ORDER BY seq"))) {
        m_error = query.lastError().text();
        return actions;
    }
    while (query.next()) {
        const std::optional<Kind> kind = kindFromName(query.value(1).toString());
        if (!kind)
            continue;
        actions.append({query.value(0).toLongLong(), *kind, query.value(2).toString(), query.value(3).toLongLong()});
    }
    return actions;
}

bool Database::removePendingActions(const QList<qint64> &seqs)
{
    Transaction transaction(m_db);
    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM pending_actions WHERE seq = ?"));
    for (qint64 seq : seqs) {
        remove.addBindValue(seq);
        if (!remove.exec()) {
            m_error = remove.lastError().text();
            return false;
        }
    }
    return transaction.commit();
}

QString Database::syncState(const QString &key)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM sync_state WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return QString();
    }
    return query.next() ? query.value(0).toString() : QString();
}

bool Database::setSyncState(const QString &key, const QString &value)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_state (key, value) VALUES (?, ?)"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::setRead(const QString &itemId, bool read)
{
    return setFlag(QStringLiteral("read"), Kind::Read, itemId, read);
}

bool Database::setStarred(const QString &itemId, bool starred)
{
    return setFlag(QStringLiteral("starred"), Kind::Starred, itemId, starred);
}

bool Database::setFlag(const QString &column, Kind kind, const QString &itemId, bool value)
{
    Transaction transaction(m_db);
    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE items SET %1 = ? WHERE id = ? AND %1 != ?").arg(column));
    update.addBindValue(value ? 1 : 0);
    update.addBindValue(itemId.toLongLong());
    update.addBindValue(value ? 1 : 0);
    if (!update.exec()) {
        m_error = update.lastError().text();
        return false;
    }
    // Only queue real changes; unknown items and no-ops send nothing.
    if (update.numRowsAffected() > 0 && !queuePendingAction(kind, itemId, value ? 1 : 0))
        return false;
    return transaction.commit();
}

bool Database::markAllRead(const QString &streamId, qint64 olderThanUsec)
{
    const std::optional<QString> condition = streamCondition(streamId);
    if (!condition) {
        m_error = QStringLiteral("Unsupported stream %1").arg(streamId);
        return false;
    }

    Transaction transaction(m_db);
    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE items SET read = 1 WHERE read = 0 AND crawled_usec <= :older AND (%1)")
                       .arg(*condition));
    update.bindValue(QStringLiteral(":older"), olderThanUsec);
    if (condition->contains(QLatin1String(":stream")))
        update.bindValue(QStringLiteral(":stream"), streamId);
    if (!update.exec()) {
        m_error = update.lastError().text();
        return false;
    }
    if (!queuePendingAction(Kind::MarkAllRead, streamId, olderThanUsec))
        return false;
    return transaction.commit();
}

bool Database::queuePendingAction(Kind kind, const QString &target, qint64 value)
{
    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral("INSERT OR REPLACE INTO pending_actions (kind, target, value) VALUES (?, ?, ?)"));
    insert.addBindValue(kindName(kind));
    insert.addBindValue(target);
    insert.addBindValue(value);
    if (!insert.exec()) {
        m_error = insert.lastError().text();
        return false;
    }
    return true;
}

std::optional<Database::StoredItem> Database::item(const QString &id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, feed_id, title, author, url, content, published, crawled_usec, read, starred "
                                 "FROM items WHERE id = ?"));
    query.addBindValue(id.toLongLong());
    if (!query.exec()) {
        m_error = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;

    StoredItem item;
    item.id = query.value(0).toString();
    item.feedId = query.value(1).toString();
    item.title = query.value(2).toString();
    item.author = query.value(3).toString();
    item.url = query.value(4).toString();
    item.contentHtml = query.value(5).toString();
    if (!query.value(6).isNull())
        item.published = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong() * 1000, Qt::UTC);
    item.crawledUsec = query.value(7).toLongLong();
    item.read = query.value(8).toBool();
    item.starred = query.value(9).toBool();
    return item;
}

QList<Database::StreamSummary> Database::folders()
{
    QList<StreamSummary> folders;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT f.category_id, f.category_label, COALESCE(SUM(i.read = 0), 0) FROM feeds f "
            "LEFT JOIN items i ON i.feed_id = f.id "
            "WHERE f.category_id != '' GROUP BY f.category_id ORDER BY f.category_label COLLATE NOCASE"))) {
        m_error = query.lastError().text();
        return folders;
    }
    while (query.next())
        folders.append({query.value(0).toString(), query.value(1).toString(), QString(), query.value(2).toInt()});
    return folders;
}

QList<Database::StreamSummary> Database::feeds(const QString &folderId)
{
    QList<StreamSummary> feeds;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT f.id, f.title, f.icon_url, COALESCE(SUM(i.read = 0), 0) FROM feeds f "
        "LEFT JOIN items i ON i.feed_id = f.id "
        "WHERE f.category_id = ? GROUP BY f.id ORDER BY f.title COLLATE NOCASE"));
    query.addBindValue(folderId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return feeds;
    }
    while (query.next()) {
        feeds.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                      query.value(3).toInt()});
    }
    return feeds;
}

QList<Database::ItemSummary> Database::items(const QString &streamId, bool unreadOnly)
{
    QList<ItemSummary> items;
    const std::optional<QString> condition = streamCondition(streamId);
    if (!condition) {
        m_error = QStringLiteral("Unsupported stream %1").arg(streamId);
        return items;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT i.id, f.title, i.title, i.author, i.published, i.crawled_usec, i.read, i.starred "
                                 "FROM items i LEFT JOIN feeds f ON f.id = i.feed_id "
                                 "WHERE (%1) %2 ORDER BY i.crawled_usec DESC")
                      .arg(*condition, unreadOnly ? QStringLiteral("AND i.read = 0") : QString()));
    if (condition->contains(QLatin1String(":stream")))
        query.bindValue(QStringLiteral(":stream"), streamId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return items;
    }
    while (query.next()) {
        ItemSummary item;
        item.id = query.value(0).toString();
        item.feedTitle = query.value(1).toString();
        item.title = query.value(2).toString();
        item.author = query.value(3).toString();
        if (!query.value(4).isNull())
            item.published = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong() * 1000, Qt::UTC);
        item.crawledUsec = query.value(5).toLongLong();
        item.read = query.value(6).toBool();
        item.starred = query.value(7).toBool();
        items.append(item);
    }
    return items;
}

QString Database::feedTitle(const QString &feedId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT title FROM feeds WHERE id = ?"));
    query.addBindValue(feedId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return QString();
    }
    return query.next() ? query.value(0).toString() : QString();
}

int Database::starredCount()
{
    return count(QStringLiteral("SELECT COUNT(*) FROM items WHERE starred = 1"));
}

bool Database::clear()
{
    Transaction transaction(m_db);
    for (const char *table : {"feeds", "items", "pending_actions", "sync_state"}) {
        if (!exec(QStringLiteral("DELETE FROM %1").arg(QLatin1String(table))))
            return false;
    }
    return transaction.commit();
}

int Database::itemCount()
{
    return count(QStringLiteral("SELECT COUNT(*) FROM items"));
}

int Database::feedCount()
{
    return count(QStringLiteral("SELECT COUNT(*) FROM feeds"));
}

int Database::unreadCount(const QString &streamId)
{
    const std::optional<QString> condition = streamCondition(streamId);
    if (!condition) {
        m_error = QStringLiteral("Unsupported stream %1").arg(streamId);
        return -1;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM items WHERE read = 0 AND (%1)").arg(*condition));
    if (condition->contains(QLatin1String(":stream")))
        query.bindValue(QStringLiteral(":stream"), streamId);
    if (!query.exec() || !query.next()) {
        m_error = query.lastError().text();
        return -1;
    }
    return query.value(0).toInt();
}

bool Database::exec(const QString &sql)
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        m_error = QStringLiteral("%1 (%2)").arg(query.lastError().text(), sql.left(60));
        return false;
    }
    return true;
}

int Database::count(const QString &sql)
{
    QSqlQuery query(m_db);
    if (!query.exec(sql) || !query.next()) {
        m_error = query.lastError().text();
        return -1;
    }
    return query.value(0).toInt();
}
