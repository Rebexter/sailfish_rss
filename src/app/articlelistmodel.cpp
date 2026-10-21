#include "articlelistmodel.h"

#include "appcontroller.h"

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
        connect(m_controller, &AppController::articlesChanged, this, &ArticleListModel::reload);
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

void ArticleListModel::markAllRead()
{
    if (!m_controller || m_rows.isEmpty())
        return;
    qint64 newest = 0;
    for (const Database::ItemSummary &row : m_rows)
        newest = qMax(newest, row.crawledUsec);
    m_controller->markAllRead(m_streamId, newest);
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
