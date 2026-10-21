#include "appcontroller.h"

#include "content/articlehtml.h"

#include <QDir>
#include <QStandardPaths>

namespace {

const QString ServerKey = QStringLiteral("account/server");  // as the user entered it
const QString ApiUrlKey = QStringLiteral("account/apiUrl");
const QString UserKey = QStringLiteral("account/user");
// TODO(phase 4): keep the session token in the keychain (Sailfish.Secrets)
// instead of the settings file. The API password itself is never stored.
const QString TokenKey = QStringLiteral("account/token");
const QString LastSyncKey = QStringLiteral("sync/lastSync");

// Wait a moment after a user action, so several actions go in one request.
const int SendDelayMs = 3000;

}  // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_sync(&m_client, &m_database)
{
    m_sendTimer.setSingleShot(true);
    m_sendTimer.setInterval(SendDelayMs);
    connect(&m_sendTimer, &QTimer::timeout, this, [this]() {
        if (!m_loggedIn)
            return;
        if (m_sync.syncing()) {
            m_sendTimer.start();
            return;
        }
        m_sync.sendPendingActions();
    });
    connect(&m_sync, &SyncManager::finished, this, &AppController::onSyncFinished);
}

bool AppController::open()
{
    // Sailjail only allows writing inside these per-app directories, so
    // the settings file goes in AppConfigLocation, not next to it.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(configDir)) {
        qWarning("Cannot create %s", qPrintable(configDir));
        return false;
    }
    m_settings.reset(new QSettings(configDir + QStringLiteral("/settings.ini"), QSettings::IniFormat));
    if (!m_database.open(dataDir + QStringLiteral("/sailrss.sqlite"))) {
        qWarning("Cannot open the database: %s", qPrintable(m_database.lastError()));
        return false;
    }

    const QString apiUrl = m_settings->value(ApiUrlKey).toString();
    const QString token = m_settings->value(TokenKey).toString();
    if (!apiUrl.isEmpty() && !token.isEmpty()) {
        m_client.setServerUrl(QUrl(apiUrl));
        m_client.setAuthToken(token);
        m_loggedIn = true;
        QTimer::singleShot(0, this, &AppController::sync);
    }
    return true;
}

Database *AppController::database()
{
    return &m_database;
}

bool AppController::loggedIn() const
{
    return m_loggedIn;
}

QString AppController::serverUrl() const
{
    return m_settings ? m_settings->value(ServerKey).toString() : QString();
}

QString AppController::userName() const
{
    return m_settings ? m_settings->value(UserKey).toString() : QString();
}

bool AppController::loggingIn() const
{
    return m_loggingIn;
}

bool AppController::syncing() const
{
    // Sending user actions in the background is not shown as syncing.
    return m_fullSyncRunning;
}

QDateTime AppController::lastSync() const
{
    return m_settings ? m_settings->value(LastSyncKey).toDateTime() : QDateTime();
}

QString AppController::syncError() const
{
    return m_syncError;
}

int AppController::unreadCount()
{
    return m_loggedIn ? qMax(0, m_database.unreadCount(GReader::StreamId::ReadingList)) : 0;
}

void AppController::login(const QString &serverUrl, const QString &user, const QString &apiPassword)
{
    if (m_loggingIn)
        return;
    const QUrl url = QUrl::fromUserInput(serverUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        emit loginFinished(false, tr("Enter the address of your FreshRSS server"));
        return;
    }

    m_loggingIn = true;
    emit loggingInChanged();
    const QString trimmedUser = user.trimmed();
    m_client.setServerUrl(url);
    m_client.login(trimmedUser, apiPassword, [this, url, trimmedUser](const GReader::Error &error) {
        m_loggingIn = false;
        emit loggingInChanged();
        if (!error.ok()) {
            emit loginFinished(false, error.message);
            return;
        }

        // A different account or server starts with an empty database.
        const QString apiUrl = m_client.apiUrl().toString();
        if (apiUrl != m_settings->value(ApiUrlKey).toString() || trimmedUser != m_settings->value(UserKey).toString()) {
            m_database.clear();
            m_settings->remove(LastSyncKey);
            emit lastSyncChanged();
            emit articlesChanged();
            emit unreadCountChanged();
        }
        m_settings->setValue(ServerKey, url.toString());
        m_settings->setValue(ApiUrlKey, apiUrl);
        m_settings->setValue(UserKey, trimmedUser);
        m_settings->setValue(TokenKey, m_client.authToken());
        m_settings->sync();

        m_loggedIn = true;
        setSyncError(QString());
        emit accountChanged();
        emit loginFinished(true, QString());
        sync();
    });
}

void AppController::logout()
{
    m_sendTimer.stop();
    m_database.clear();
    m_settings->remove(TokenKey);
    m_settings->remove(LastSyncKey);
    m_settings->sync();
    m_client.setAuthToken(QString());
    m_loggedIn = false;
    setSyncError(QString());
    emit accountChanged();
    emit lastSyncChanged();
    emit articlesChanged();
    emit unreadCountChanged();
}

void AppController::sync()
{
    if (!m_loggedIn)
        return;
    if (m_sync.syncing()) {
        m_syncRequested = true;
        return;
    }
    m_sendTimer.stop();  // the full sync sends pending actions first
    m_fullSyncRunning = true;
    emit syncingChanged();
    m_sync.sync();
}

void AppController::onSyncFinished(bool ok)
{
    const bool wasFull = m_fullSyncRunning;
    if (wasFull) {
        m_fullSyncRunning = false;
        emit syncingChanged();
    }

    if (ok) {
        setSyncError(QString());
        if (wasFull) {
            m_settings->setValue(LastSyncKey, QDateTime::currentDateTimeUtc());
            emit lastSyncChanged();
        }
    } else {
        switch (m_sync.lastErrorKind()) {
        case SyncManager::ErrorKind::Network:
            // Being offline is normal; failed background sends stay queued.
            if (wasFull)
                setSyncError(tr("Cannot reach the server"));
            break;
        case SyncManager::ErrorKind::Auth:
            m_settings->remove(TokenKey);
            m_loggedIn = false;
            setSyncError(tr("The session is no longer valid, please log in again"));
            emit accountChanged();
            break;
        case SyncManager::ErrorKind::Server:
            setSyncError(tr("Server error: %1").arg(m_sync.lastErrorMessage()));
            break;
        case SyncManager::ErrorKind::Storage:
            setSyncError(tr("Storage error: %1").arg(m_sync.lastErrorMessage()));
            break;
        case SyncManager::ErrorKind::None:
            break;
        }
    }

    // Even a failed full sync may have stored some articles.
    if (wasFull) {
        emit articlesChanged();
        emit unreadCountChanged();
    }

    if (m_syncRequested) {
        m_syncRequested = false;
        sync();
    } else if (ok && !m_database.pendingActions().isEmpty()) {
        scheduleSend();  // actions taken while this sync was running
    }
}

void AppController::setSyncError(const QString &message)
{
    if (m_syncError == message)
        return;
    m_syncError = message;
    emit syncErrorChanged();
}

void AppController::setRead(const QString &itemId, bool read)
{
    if (!m_database.setRead(itemId, read)) {
        qWarning("setRead failed: %s", qPrintable(m_database.lastError()));
        return;
    }
    emitArticleChanged(itemId);
    scheduleSend();
}

void AppController::setStarred(const QString &itemId, bool starred)
{
    if (!m_database.setStarred(itemId, starred)) {
        qWarning("setStarred failed: %s", qPrintable(m_database.lastError()));
        return;
    }
    emitArticleChanged(itemId);
    scheduleSend();
}

void AppController::markAllRead(const QString &streamId, qint64 olderThanUsec)
{
    if (!m_database.markAllRead(streamId, olderThanUsec)) {
        qWarning("markAllRead failed: %s", qPrintable(m_database.lastError()));
        return;
    }
    emit articlesChanged();
    emit unreadCountChanged();
    scheduleSend();
}

QVariantMap AppController::article(const QString &itemId, const QString &linkColor)
{
    QVariantMap result;
    const std::optional<Database::StoredItem> item = m_database.item(itemId);
    if (!item)
        return result;
    result.insert(QStringLiteral("id"), item->id);
    result.insert(QStringLiteral("title"), item->title);
    result.insert(QStringLiteral("feedTitle"), m_database.feedTitle(item->feedId));
    result.insert(QStringLiteral("author"), item->author);
    result.insert(QStringLiteral("url"), item->url);
    result.insert(QStringLiteral("published"), item->published);
    result.insert(QStringLiteral("read"), item->read);
    result.insert(QStringLiteral("starred"), item->starred);
    result.insert(QStringLiteral("html"), ArticleHtml::forRichText(item->contentHtml, linkColor));
    return result;
}

void AppController::scheduleSend()
{
    if (m_loggedIn)
        m_sendTimer.start();
}

void AppController::emitArticleChanged(const QString &itemId)
{
    const std::optional<Database::StoredItem> item = m_database.item(itemId);
    if (item)
        emit articleChanged(itemId, item->read, item->starred);
    emit unreadCountChanged();
}
