#include "appcontroller.h"

#include "app/imagecache.h"
#include "content/articlehtml.h"

#include <QDir>
#include <QStandardPaths>

namespace {

const QString ServerKey = QStringLiteral("account/server");  // as the user entered it
const QString ApiUrlKey = QStringLiteral("account/apiUrl");
const QString UserKey = QStringLiteral("account/user");
// Early development versions kept the session token here; it is moved to the
// keychain on start.
const QString LegacyTokenKey = QStringLiteral("account/token");
// The session token lives in the keychain; the API password is never stored.
const QString TokenSecret = QStringLiteral("sessionToken");
const QString LastSyncKey = QStringLiteral("sync/lastSync");
const QString RefreshIntervalKey = QStringLiteral("sync/refreshInterval");
const int DefaultRefreshInterval = 30;
const QString OldestFirstKey = QStringLiteral("view/oldestFirst");
const QString ReadRetentionKey = QStringLiteral("storage/readRetention");
const int DefaultReadRetention = 30;

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

    QString token;
    const QString legacyToken = m_settings->value(LegacyTokenKey).toString();
    if (!legacyToken.isEmpty()) {
        if (m_keychain.store(TokenSecret, legacyToken.toUtf8())) {
            m_settings->remove(LegacyTokenKey);
            m_settings->sync();
        } else {
            qWarning("Cannot move the session token to the keychain: %s", qPrintable(m_keychain.lastError()));
        }
        token = legacyToken;
    }

    const QString apiUrl = m_settings->value(ApiUrlKey).toString();
    if (!apiUrl.isEmpty() && token.isEmpty()) {
        const std::optional<QByteArray> stored = m_keychain.load(TokenSecret);
        if (stored)
            token = QString::fromUtf8(*stored);
        else
            qInfo("No session token in the keychain: %s", qPrintable(m_keychain.lastError()));
    }
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

int AppController::refreshInterval() const
{
    return m_settings ? m_settings->value(RefreshIntervalKey, DefaultRefreshInterval).toInt() : DefaultRefreshInterval;
}

void AppController::setRefreshInterval(int minutes)
{
    if (!m_settings || minutes == refreshInterval())
        return;
    m_settings->setValue(RefreshIntervalKey, qMax(0, minutes));
    emit refreshIntervalChanged();
}

bool AppController::oldestFirst() const
{
    return m_settings && m_settings->value(OldestFirstKey, false).toBool();
}

void AppController::setOldestFirst(bool oldestFirst)
{
    if (!m_settings || oldestFirst == this->oldestFirst())
        return;
    m_settings->setValue(OldestFirstKey, oldestFirst);
    emit oldestFirstChanged();
}

int AppController::readRetention() const
{
    return m_settings ? m_settings->value(ReadRetentionKey, DefaultReadRetention).toInt() : DefaultReadRetention;
}

void AppController::setReadRetention(int days)
{
    if (!m_settings || days == readRetention())
        return;
    m_settings->setValue(ReadRetentionKey, qMax(0, days));
    emit readRetentionChanged();

    // A shorter time applies right away rather than at the next sync.
    if (days > 0) {
        const qint64 readBefore = QDateTime::currentMSecsSinceEpoch() / 1000 - qint64(days) * 24 * 3600;
        if (m_database.deleteExpired(readBefore) > 0)
            emit articlesChanged();
    }
}

bool AppController::syncDue() const
{
    const int interval = refreshInterval();
    if (!m_loggedIn || interval <= 0)
        return false;
    const QDateTime last = lastSync();
    return !last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) >= qint64(interval) * 60;
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

        if (!m_keychain.store(TokenSecret, m_client.authToken().toUtf8())) {
            m_client.setAuthToken(QString());
            emit loginFinished(false, tr("Cannot save the session in the keychain: %1").arg(m_keychain.lastError()));
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
    m_settings->remove(LastSyncKey);
    m_settings->sync();
    ImageCacheFactory::clear();
    forgetSession();
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
    m_sync.setReadRetentionDays(readRetention());
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
            // Readable text instead of QSettings' binary QDateTime; both load.
            m_settings->setValue(LastSyncKey, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
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
            forgetSession();
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

QVariantMap AppController::article(const QString &itemId)
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
    return result;
}

QVariantList AppController::articleBlocks(const QString &itemId, const QString &linkColor)
{
    QVariantList result;
    const std::optional<Database::StoredItem> item = m_database.item(itemId);
    if (!item)
        return result;
    for (const ArticleHtml::Block &block : ArticleHtml::blocks(item->contentHtml, QUrl(item->url), linkColor)) {
        QVariantMap map;
        if (block.type == ArticleHtml::Block::Type::Text) {
            map.insert(QStringLiteral("type"), QStringLiteral("text"));
            map.insert(QStringLiteral("html"), block.html);
        } else {
            map.insert(QStringLiteral("type"), QStringLiteral("image"));
            map.insert(QStringLiteral("image"), block.image.toString());
            map.insert(QStringLiteral("link"), block.link.toString());
        }
        result.append(map);
    }
    return result;
}

void AppController::scheduleSend()
{
    if (m_loggedIn)
        m_sendTimer.start();
}

// Keeps server and user for the login page, but not the token.
void AppController::forgetSession()
{
    if (!m_keychain.remove(TokenSecret))
        qInfo("Removing the session token: %s", qPrintable(m_keychain.lastError()));
    m_client.setAuthToken(QString());
    m_loggedIn = false;
}

void AppController::emitArticleChanged(const QString &itemId)
{
    const std::optional<Database::StoredItem> item = m_database.item(itemId);
    if (item)
        emit articleChanged(itemId, item->read, item->starred);
    emit unreadCountChanged();
}
