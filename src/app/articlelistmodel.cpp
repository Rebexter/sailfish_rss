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

bool ArticleListModel::unreadOnly() const
{
    return m_unreadOnly;
}

void ArticleListModel::setUnreadOnly(bool unreadOnly)
{
    if (m_unreadOnly == unreadOnly)
        return;
    m_unreadOnly = unreadOnly;
    emit unreadOnlyChanged();
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
        rows = m_controller->database()->items(m_streamId, m_unreadOnly);

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
    const QList<Database::ItemSummary> fresh = db->items(m_streamId, m_unreadOnly);
    QHash<QString, int> freshIndex;
    for (int i = 0; i < fresh.size(); ++i)
        freshIndex.insert(fresh.at(i).id, i);

    // Update or remove the listed articles. One missing from the fresh list
    // was deleted, or no longer matches it (read in an unread-only list,
    // unstarred in Starred); the latter stay with their new state.
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

    // Insert new articles; both lists are sorted newest arrival first.
    QSet<QString> listed;
    for (const Database::ItemSummary &row : m_rows)
        listed.insert(row.id);
    int position = 0;
    for (const Database::ItemSummary &item : fresh) {
        if (listed.contains(item.id))
            continue;
        while (position < m_rows.size() && m_rows.at(position).crawledUsec > item.crawledUsec)
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
