#include "syncmanager.h"

#include <QDateTime>
#include <QPointer>

namespace StreamId = GReader::StreamId;
using Kind = Database::PendingAction::Kind;

namespace {

const QString LastSyncKey = QStringLiteral("lastSyncTime");
const int ArticlePageSize = 100;
const int IdPageSize = 1000;
const int SendBatchSize = 250;  // item IDs per write request
const int FetchBatchSize = 100;
// Fetch a little before the last sync, so articles that arrived while it
// was running are not missed.
const qint64 SyncOverlapSecs = 300;

qint64 nowSecs()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000;
}

}  // namespace

SyncManager::SyncManager(GReader::Client *client, Database *database, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_database(database)
{
}

void SyncManager::setHistoryDays(int days)
{
    m_historyDays = days;
}

bool SyncManager::syncing() const
{
    return m_syncing;
}

SyncManager::ErrorKind SyncManager::lastErrorKind() const
{
    return m_errorKind;
}

QString SyncManager::lastErrorMessage() const
{
    return m_errorMessage;
}

SyncManager::Stats SyncManager::lastStats() const
{
    return m_stats;
}

void SyncManager::sync()
{
    if (m_syncing)
        return;
    m_syncing = true;
    m_errorKind = ErrorKind::None;
    m_errorMessage.clear();
    m_stats = Stats();
    m_serverTime = 0;
    m_unreadIds.clear();
    m_starredIds.clear();

    const qint64 windowStart = nowSecs() - qint64(m_historyDays) * 24 * 3600;
    const qint64 lastSync = m_database->syncState(LastSyncKey).toLongLong();
    m_since = qMax(windowStart, lastSync - SyncOverlapSecs);

    emit syncingChanged();
    sendPending();
}

// Consecutive actions of the same kind and value go into one request;
// mark-all-read is sent on its own. Order is preserved, so e.g. "unread"
// followed by "mark all read" ends up read on the server too.
void SyncManager::sendPending()
{
    m_batches.clear();
    for (const Database::PendingAction &action : m_database->pendingActions()) {
        if (action.kind != Kind::MarkAllRead && !m_batches.isEmpty()) {
            Batch &last = m_batches.last();
            if (last.kind == action.kind && last.value == action.value && last.targets.size() < SendBatchSize) {
                last.targets.append(action.target);
                last.seqs.append(action.seq);
                continue;
            }
        }
        m_batches.append({action.kind, action.value, {action.target}, {action.seq}});
    }
    sendNextBatch();
}

void SyncManager::sendNextBatch()
{
    if (m_batches.isEmpty()) {
        fetchSubscriptions();
        return;
    }

    const Batch batch = m_batches.takeFirst();
    QPointer<SyncManager> self(this);
    const auto done = [self, batch](const GReader::Error &error) {
        if (!self || !self->check(error))
            return;
        if (!self->checkStorage(self->m_database->removePendingActions(batch.seqs)))
            return;
        self->m_stats.sent += batch.seqs.size();
        self->sendNextBatch();
    };

    switch (batch.kind) {
    case Kind::Read:
        m_client->setRead(batch.targets, batch.value != 0, done);
        break;
    case Kind::Starred:
        m_client->setStarred(batch.targets, batch.value != 0, done);
        break;
    case Kind::MarkAllRead:
        m_client->markAllAsRead(batch.targets.first(), batch.value, done);
        break;
    }
}

void SyncManager::fetchSubscriptions()
{
    QPointer<SyncManager> self(this);
    m_client->fetchSubscriptions([self](const GReader::Error &error, const QList<GReader::Subscription> &subscriptions) {
        if (!self || !self->check(error))
            return;
        if (!self->checkStorage(self->m_database->replaceSubscriptions(subscriptions)))
            return;
        self->fetchArticles(QString());
    });
}

void SyncManager::fetchArticles(const QString &continuation)
{
    GReader::StreamQuery query;
    query.count = ArticlePageSize;
    query.newerThan = m_since;
    query.continuation = continuation;

    QPointer<SyncManager> self(this);
    m_client->fetchStream(query, [self](const GReader::Error &error, const GReader::StreamPage &page) {
        if (!self || !self->check(error))
            return;
        if (self->m_serverTime == 0)
            self->m_serverTime = page.updated;
        if (!self->checkStorage(self->m_database->upsertItems(page.items)))
            return;
        self->m_stats.fetched += page.items.size();

        if (!page.continuation.isEmpty() && !page.items.isEmpty()) {
            self->fetchArticles(page.continuation);
            return;
        }

        GReader::StreamQuery unread;
        unread.count = IdPageSize;
        unread.excludeTarget = StreamId::Read;
        self->fetchIds(unread, &self->m_unreadIds, [self]() {
            GReader::StreamQuery starred;
            starred.streamId = StreamId::Starred;
            starred.count = IdPageSize;
            self->fetchIds(starred, &self->m_starredIds, [self]() { self->reconcile(); });
        });
    });
}

void SyncManager::fetchIds(const GReader::StreamQuery &query, QStringList *ids, const std::function<void()> &next)
{
    QPointer<SyncManager> self(this);
    m_client->fetchItemIds(query, [self, query, ids, next](const GReader::Error &error, const GReader::ItemIdsPage &page) {
        if (!self || !self->check(error))
            return;
        ids->append(page.ids);
        if (!page.continuation.isEmpty() && !page.ids.isEmpty()) {
            GReader::StreamQuery nextPage = query;
            nextPage.continuation = page.continuation;
            self->fetchIds(nextPage, ids, next);
            return;
        }
        next();
    });
}

void SyncManager::reconcile()
{
    if (!checkStorage(m_database->reconcileStates(m_unreadIds, m_starredIds)))
        return;
    fetchMissingStarred(m_database->missingItemIds(m_starredIds));
}

void SyncManager::fetchMissingStarred(QStringList missing)
{
    if (missing.isEmpty()) {
        cleanUp();
        return;
    }

    const QStringList batch = missing.mid(0, FetchBatchSize);
    missing = missing.mid(FetchBatchSize);
    QPointer<SyncManager> self(this);
    m_client->fetchItems(batch, [self, missing](const GReader::Error &error, const GReader::StreamPage &page) {
        if (!self || !self->check(error))
            return;
        if (!self->checkStorage(self->m_database->upsertItems(page.items)))
            return;
        self->m_stats.fetched += page.items.size();
        self->fetchMissingStarred(missing);
    });
}

void SyncManager::cleanUp()
{
    const qint64 cutoffUsec = (nowSecs() - qint64(m_historyDays) * 24 * 3600) * 1000000;
    const int deleted = m_database->deleteExpired(cutoffUsec);
    if (!checkStorage(deleted >= 0))
        return;
    m_stats.deleted = deleted;

    const qint64 syncTime = m_serverTime > 0 ? m_serverTime : nowSecs();
    if (!checkStorage(m_database->setSyncState(LastSyncKey, QString::number(syncTime))))
        return;
    finish(ErrorKind::None, QString());
}

bool SyncManager::check(const GReader::Error &error)
{
    switch (error.type) {
    case GReader::Error::Type::None:
        return true;
    case GReader::Error::Type::Network:
        finish(ErrorKind::Network, error.message);
        break;
    case GReader::Error::Type::Auth:
        finish(ErrorKind::Auth, error.message);
        break;
    case GReader::Error::Type::Http:
    case GReader::Error::Type::Parse:
        finish(ErrorKind::Server, error.message);
        break;
    }
    return false;
}

bool SyncManager::checkStorage(bool ok)
{
    if (!ok)
        finish(ErrorKind::Storage, m_database->lastError());
    return ok;
}

void SyncManager::finish(ErrorKind kind, const QString &message)
{
    m_syncing = false;
    m_errorKind = kind;
    m_errorMessage = message;
    m_batches.clear();
    if (kind == ErrorKind::None)
        qInfo("Sync done: %d sent, %d fetched, %d deleted", m_stats.sent, m_stats.fetched, m_stats.deleted);
    else
        qWarning("Sync failed: %s", qPrintable(message));
    emit syncingChanged();
    emit finished(kind == ErrorKind::None);
}
