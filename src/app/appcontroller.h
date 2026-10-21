#ifndef SAILRSS_APPCONTROLLER_H
#define SAILRSS_APPCONTROLLER_H

#include "api/greaderclient.h"
#include "storage/database.h"
#include "sync/syncmanager.h"

#include <QDateTime>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>
#include <memory>

// The app's state and actions, exposed to QML as "app": the account, the
// local database and sync. User actions go to the database first and are
// sent to the server shortly after.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY accountChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY accountChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY accountChanged)
    Q_PROPERTY(bool loggingIn READ loggingIn NOTIFY loggingInChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY lastSyncChanged)
    Q_PROPERTY(QString syncError READ syncError NOTIFY syncErrorChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    // Opens settings and the database in the app's data directories.
    bool open();
    Database *database();

    bool loggedIn() const;
    QString serverUrl() const;
    QString userName() const;
    bool loggingIn() const;
    bool syncing() const;
    QDateTime lastSync() const;
    QString syncError() const;
    int unreadCount();

    Q_INVOKABLE void login(const QString &serverUrl, const QString &user, const QString &apiPassword);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void sync();

    Q_INVOKABLE void setRead(const QString &itemId, bool read);
    Q_INVOKABLE void setStarred(const QString &itemId, bool starred);
    Q_INVOKABLE void markAllRead(const QString &streamId, qint64 olderThanUsec);

    // title, feedTitle, author, url, published, read, starred and html
    // (prepared for Text.RichText, links in linkColor).
    Q_INVOKABLE QVariantMap article(const QString &itemId, const QString &linkColor);

signals:
    void accountChanged();
    void loggingInChanged();
    void syncingChanged();
    void lastSyncChanged();
    void syncErrorChanged();
    void unreadCountChanged();
    void loginFinished(bool ok, const QString &message);
    // Lists need to be reloaded (after a sync, mark all read or logout).
    void articlesChanged();
    // One article's state changed; lists can update it in place.
    void articleChanged(const QString &itemId, bool read, bool starred);

private:
    void onSyncFinished(bool ok);
    void setSyncError(const QString &message);
    void scheduleSend();
    void emitArticleChanged(const QString &itemId);

    std::unique_ptr<QSettings> m_settings;
    Database m_database;
    GReader::Client m_client;
    SyncManager m_sync;
    QTimer m_sendTimer;
    bool m_loggedIn = false;
    bool m_loggingIn = false;
    bool m_fullSyncRunning = false;
    bool m_syncRequested = false;
    QString m_syncError;
};

#endif  // SAILRSS_APPCONTROLLER_H
