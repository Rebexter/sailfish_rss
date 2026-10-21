#ifndef SAILRSS_ARTICLELISTMODEL_H
#define SAILRSS_ARTICLELISTMODEL_H

#include "storage/database.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlParserStatus>

class AppController;

// Articles of one stream by arrival, newest or oldest first, optionally
// only unread or only read ones. An article whose state changes stays in a
// filtered list until the next reload, so it does not disappear while the
// user is reading.
class ArticleListModel : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(AppController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString streamId READ streamId WRITE setStreamId NOTIFY streamIdChanged)
    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool oldestFirst READ oldestFirst WRITE setOldestFirst NOTIFY oldestFirstChanged)

public:
    enum Filter {
        Unread,
        All,
        Read,
    };
    Q_ENUM(Filter)

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
    Filter filter() const;
    void setFilter(Filter filter);
    bool oldestFirst() const;
    void setOldestFirst(bool oldestFirst);

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
    void filterChanged();
    void oldestFirstChanged();

private:
    void updateArticle(const QString &itemId, bool read, bool starred);
    QList<Database::ItemSummary> query() const;

    QPointer<AppController> m_controller;
    QString m_streamId;
    Filter m_filter = Unread;
    bool m_oldestFirst = false;
    QList<Database::ItemSummary> m_rows;
    bool m_complete = true;
};

#endif  // SAILRSS_ARTICLELISTMODEL_H
