#include "articlelistmodel.h"

#include "appcontroller.h"

#include <QHash>
#include <QSet>

ArticleListModel::ArticleListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

AppController *ArticleListModel::controller() const
{
    return m_controller;
}

void ArticleListModel::setController(AppController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &AppController::articlesChanged, this, &ArticleListModel::refresh);
        connect(m_controller, &AppController::articleChanged, this, &ArticleListModel::updateArticle);
    }
    emit controllerChanged();
    reload();
}

QString ArticleListModel::streamId() const
{
    return m_streamId;
}

void ArticleListModel::setStreamId(const QString &streamId)
{
    if (m_streamId == streamId)
        return;
    m_streamId = streamId;
    emit streamIdChanged();
    reload();
}

ArticleListModel::Filter ArticleListModel::filter() const
{
    return m_filter;
}

void ArticleListModel::setFilter(Filter filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    reload();
}

bool ArticleListModel::oldestFirst() const
{
    return m_oldestFirst;
}

void ArticleListModel::setOldestFirst(bool oldestFirst)
{
    if (m_oldestFirst == oldestFirst)
        return;
    m_oldestFirst = oldestFirst;
    emit oldestFirstChanged();
    reload();
}

int ArticleListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ArticleListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();
    const Database::ItemSummary &row = m_rows.at(index.row());
    switch (role) {
    case ArticleIdRole:
        return row.id;
    case TitleRole:
        return row.title;
    case FeedTitleRole:
        return row.feedTitle;
    case AuthorRole:
        return row.author;
    case PublishedRole:
        return row.published;
    case ReadRole:
        return row.read;
    case StarredRole:
        return row.starred;
    }
    return QVariant();
}

QHash<int, QByteArray> ArticleListModel::roleNames() const
{
    return {{ArticleIdRole, "articleId"}, {TitleRole, "title"},   {FeedTitleRole, "feedTitle"},
            {AuthorRole, "author"},       {PublishedRole, "published"}, {ReadRole, "read"},
            {StarredRole, "starred"}};
}

void ArticleListModel::classBegin()
{
    m_complete = false;
}

void ArticleListModel::componentComplete()
{
    m_complete = true;
    reload();
}

void ArticleListModel::reload()
{
    if (!m_complete)
        return;
    QList<Database::ItemSummary> rows;
    if (m_controller && m_controller->loggedIn() && !m_streamId.isEmpty())
        rows = query();

    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void ArticleListModel::refresh()
{
    if (!m_complete)
        return;
    // Nothing to keep the position of; inserting row by row would be slow.
    if (!m_controller || !m_controller->loggedIn() || m_streamId.isEmpty() || m_rows.isEmpty()) {
        reload();
        return;
    }
    Database *db = m_controller->database();
    const QList<Database::ItemSummary> fresh = query();
    QHash<QString, int> freshIndex;
    for (int i = 0; i < fresh.size(); ++i)
        freshIndex.insert(fresh.at(i).id, i);

    // Update or remove the listed articles. One missing from the fresh list
    // was deleted, or no longer matches it (read in an unread list, unread
    // in a read list, unstarred in Starred); the latter stay with their new
    // state.
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        Database::ItemSummary &row = m_rows[i];
        bool read;
        bool starred;
        const auto found = freshIndex.constFind(row.id);
        if (found != freshIndex.constEnd()) {
            read = fresh.at(*found).read;
            starred = fresh.at(*found).starred;
        } else {
            const std::optional<Database::StoredItem> item = db->item(row.id);
            if (!item) {
                beginRemoveRows(QModelIndex(), i, i);
                m_rows.removeAt(i);
                endRemoveRows();
                continue;
            }
            read = item->read;
            starred = item->starred;
        }
        if (read != row.read || starred != row.starred) {
            row.read = read;
            row.starred = starred;
            emit dataChanged(index(i), index(i), {ReadRole, StarredRole});
        }
    }

    // Insert new articles; both lists are sorted the same way.
    QSet<QString> listed;
    for (const Database::ItemSummary &row : m_rows)
        listed.insert(row.id);
    int position = 0;
    for (const Database::ItemSummary &item : fresh) {
        if (listed.contains(item.id))
            continue;
        while (position < m_rows.size()
               && (m_oldestFirst ? m_rows.at(position).crawledUsec < item.crawledUsec
                                 : m_rows.at(position).crawledUsec > item.crawledUsec))
            ++position;
        beginInsertRows(QModelIndex(), position, position);
        m_rows.insert(position, item);
        endInsertRows();
        ++position;
    }
}

void ArticleListModel::markAllRead()
{
    if (!m_controller || m_rows.isEmpty())
        return;
    qint64 newest = 0;
    for (const Database::ItemSummary &row : m_rows)
        newest = qMax(newest, row.crawledUsec);
    m_controller->markAllRead(m_streamId, newest);
    // Unlike single articles, these should not stay in an unread-only list.
    reload();
}

QList<Database::ItemSummary> ArticleListModel::query() const
{
    Database::ItemFilter filter = Database::ItemFilter::All;
    if (m_filter == Unread)
        filter = Database::ItemFilter::Unread;
    else if (m_filter == Read)
        filter = Database::ItemFilter::Read;
    return m_controller->database()->items(m_streamId, filter, m_oldestFirst);
}

void ArticleListModel::updateArticle(const QString &itemId, bool read, bool starred)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        Database::ItemSummary &row = m_rows[i];
        if (row.id != itemId)
            continue;
        row.read = read;
        row.starred = starred;
        const QModelIndex changed = index(i);
        emit dataChanged(changed, changed, {ReadRole, StarredRole});
        return;
    }
}
