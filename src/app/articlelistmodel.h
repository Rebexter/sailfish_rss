#ifndef SAILRSS_ARTICLELISTMODEL_H
#define SAILRSS_ARTICLELISTMODEL_H

#include "storage/database.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlParserStatus>

class AppController;

// Articles of one stream, newest arrival first. Articles marked read stay
// in an unread-only list until the next reload, so they do not disappear
// while the user is reading. After a sync the list is updated in place, so
// the scroll position is kept.
class ArticleListModel : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(AppController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString streamId READ streamId WRITE setStreamId NOTIFY streamIdChanged)
    Q_PROPERTY(bool unreadOnly READ unreadOnly WRITE setUnreadOnly NOTIFY unreadOnlyChanged)

public:
    enum Role {
        ArticleIdRole = Qt::UserRole + 1,
        TitleRole,
        FeedTitleRole,
        AuthorRole,
        PublishedRole,
        ReadRole,
        StarredRole,
    };

    explicit ArticleListModel(QObject *parent = nullptr);

    AppController *controller() const;
    void setController(AppController *controller);
    QString streamId() const;
    void setStreamId(const QString &streamId);
    bool unreadOnly() const;
    void setUnreadOnly(bool unreadOnly);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void classBegin() override;
    void componentComplete() override;

    // Rebuilds the list from scratch.
    Q_INVOKABLE void reload();
    // Merges in changes from the database: inserts new articles, updates
    // read and starred state, removes deleted ones.
    Q_INVOKABLE void refresh();
    // Marks everything in the stream read up to the newest listed article,
    // so articles that arrive meanwhile stay unread.
    Q_INVOKABLE void markAllRead();

signals:
    void controllerChanged();
    void streamIdChanged();
    void unreadOnlyChanged();

private:
    void updateArticle(const QString &itemId, bool read, bool starred);

    QPointer<AppController> m_controller;
    QString m_streamId;
    bool m_unreadOnly = true;
    QList<Database::ItemSummary> m_rows;
    bool m_complete = true;
};

#endif  // SAILRSS_ARTICLELISTMODEL_H
