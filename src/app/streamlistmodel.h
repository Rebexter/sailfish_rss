#ifndef SAILRSS_STREAMLISTMODEL_H
#define SAILRSS_STREAMLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlParserStatus>

class AppController;

// Streams to pick from: without folderId "All articles", "Starred" and the
// folders; with folderId the folder itself and its feeds.
class StreamListModel : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(AppController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)

public:
    enum Kind {
        AllKind,
        StarredKind,
        FolderKind,
        FeedKind,
    };
    Q_ENUM(Kind)

    enum Role {
        StreamIdRole = Qt::UserRole + 1,
        TitleRole,
        CountRole,  // unread articles; starred articles for StarredKind
        KindRole,
        IconUrlRole,
    };

    explicit StreamListModel(QObject *parent = nullptr);

    AppController *controller() const;
    void setController(AppController *controller);
    QString folderId() const;
    void setFolderId(const QString &folderId);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void classBegin() override;
    void componentComplete() override;

    Q_INVOKABLE void reload();

signals:
    void controllerChanged();
    void folderIdChanged();

private:
    struct Row {
        QString streamId;
        QString title;
        int count;
        Kind kind;
        QString iconUrl;
    };

    QPointer<AppController> m_controller;
    QString m_folderId;
    QList<Row> m_rows;
    bool m_complete = true;
};

#endif  // SAILRSS_STREAMLISTMODEL_H
