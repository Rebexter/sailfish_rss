#include "streamlistmodel.h"

#include "appcontroller.h"

namespace StreamId = GReader::StreamId;

StreamListModel::StreamListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

AppController *StreamListModel::controller() const
{
    return m_controller;
}

void StreamListModel::setController(AppController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &AppController::articlesChanged, this, &StreamListModel::reload);
        connect(m_controller, &AppController::articleChanged, this, &StreamListModel::reload);
    }
    emit controllerChanged();
    reload();
}

QString StreamListModel::folderId() const
{
    return m_folderId;
}

void StreamListModel::setFolderId(const QString &folderId)
{
    if (m_folderId == folderId)
        return;
    m_folderId = folderId;
    emit folderIdChanged();
    reload();
}

int StreamListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant StreamListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case StreamIdRole:
        return row.streamId;
    case TitleRole:
        return row.title;
    case CountRole:
        return row.count;
    case KindRole:
        return row.kind;
    case IconUrlRole:
        return row.iconUrl;
    }
    return QVariant();
}

QHash<int, QByteArray> StreamListModel::roleNames() const
{
    return {{StreamIdRole, "streamId"}, {TitleRole, "title"}, {CountRole, "count"},
            {KindRole, "kind"},         {IconUrlRole, "iconUrl"}};
}

void StreamListModel::classBegin()
{
    m_complete = false;
}

void StreamListModel::componentComplete()
{
    m_complete = true;
    reload();
}

void StreamListModel::reload()
{
    if (!m_complete)
        return;

    QList<Row> rows;
    if (m_controller && m_controller->loggedIn()) {
        Database *db = m_controller->database();
        if (m_folderId.isEmpty()) {
            rows.append({StreamId::ReadingList, tr("All articles"), db->unreadCount(StreamId::ReadingList), AllKind,
                         QString()});
            rows.append({StreamId::Starred, tr("Starred"), db->starredCount(), StarredKind, QString()});
            for (const Database::StreamSummary &folder : db->folders())
                rows.append({folder.id, folder.title, folder.unread, FolderKind, QString()});
        } else {
            rows.append({m_folderId, tr("All articles"), db->unreadCount(m_folderId), AllKind, QString()});
            for (const Database::StreamSummary &feed : db->feeds(m_folderId))
                rows.append({feed.id, feed.title, feed.unread, FeedKind, feed.iconUrl});
        }
    }

    bool sameStreams = rows.size() == m_rows.size();
    for (int i = 0; sameStreams && i < rows.size(); ++i)
        sameStreams = rows.at(i).streamId == m_rows.at(i).streamId && rows.at(i).title == m_rows.at(i).title;
    if (!sameStreams) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).count != m_rows.at(i).count || rows.at(i).iconUrl != m_rows.at(i).iconUrl) {
            m_rows[i] = rows.at(i);
            emit dataChanged(index(i), index(i), {CountRole, IconUrlRole});
        }
    }
}
