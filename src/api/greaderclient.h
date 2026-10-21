#ifndef SAILRSS_GREADERCLIENT_H
#define SAILRSS_GREADERCLIENT_H

#include "greadertypes.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QUrl>
#include <functional>

class QNetworkReply;

namespace GReader {

using Params = QList<QPair<QString, QString>>;

// application/x-www-form-urlencoded, also used for query strings. Unlike
// QUrlQuery it encodes '+', which PHP would otherwise decode as a space.
QByteArray encodeForm(const Params &params);

struct Error {
    enum class Type {
        None,
        Network,  // no connection, TLS failure, timeout
        Auth,     // HTTP 401: wrong credentials or expired session
        Http,     // any other HTTP error status
        Parse,    // unexpected response body
    };
    Type type = Type::None;
    int httpStatus = 0;
    QString message;

    bool ok() const { return type == Type::None; }
};

struct StreamQuery {
    QString streamId = StreamId::ReadingList;
    int count = 100;
    QString continuation;
    qint64 newerThan = 0;   // Unix seconds; also matches items changed since
    QString excludeTarget;  // e.g. StreamId::Read for unread items only
    QString includeTarget;  // e.g. StreamId::Starred
    bool oldestFirst = false;
};

// Client for the Google Reader compatible API of FreshRSS. All calls are
// asynchronous; callbacks run on the thread that owns the client.
class Client : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const Error &)>;
    template <typename T>
    using ResultCallback = std::function<void(const Error &, const T &)>;

    explicit Client(QObject *parent = nullptr);

    // Accepts the FreshRSS address as users know it (https://host/,
    // https://host/i/ or the API address itself) and returns the API base,
    // https://host/api/greader.php.
    static QUrl apiUrlFromServerUrl(const QUrl &serverUrl);

    void setServerUrl(const QUrl &serverUrl);
    QUrl apiUrl() const;

    // The session token from login(). FreshRSS tokens do not expire unless
    // the API password changes, so it can be stored instead of the password.
    void setAuthToken(const QString &token);
    QString authToken() const;

    void setTimeout(int milliseconds);

    void login(const QString &user, const QString &apiPassword, const Callback &done);

    void fetchSubscriptions(const ResultCallback<QList<Subscription>> &done);
    void fetchTags(const ResultCallback<QList<Tag>> &done);
    void fetchUnreadCounts(const ResultCallback<QList<UnreadCount>> &done);
    void fetchStream(const StreamQuery &query, const ResultCallback<StreamPage> &done);
    void fetchItemIds(const StreamQuery &query, const ResultCallback<ItemIdsPage> &done);

    // Adds and removes stream IDs (e.g. StreamId::Read) on the given items.
    void editTags(const QStringList &itemIds, const QStringList &add, const QStringList &remove,
                  const Callback &done);
    void setRead(const QStringList &itemIds, bool read, const Callback &done);
    void setStarred(const QStringList &itemIds, bool starred, const Callback &done);

    // Marks everything in the stream read that arrived before olderThanUsec
    // (an item's crawledUsec), so items that arrived meanwhile stay unread.
    void markAllAsRead(const QString &streamId, qint64 olderThanUsec, const Callback &done);

private:
    using BodyCallback = std::function<void(const Error &, const QByteArray &)>;

    void get(const QString &path, const Params &query, const BodyCallback &done);
    void post(const QString &path, const Params &form, const BodyCallback &done);
    void postWithToken(const QString &path, Params form, const Callback &done);
    void send(QNetworkReply *reply, const BodyCallback &done);
    QNetworkRequest request(const QString &path, const Params &query) const;
    static Params streamParams(const StreamQuery &query);

    QNetworkAccessManager m_network;
    QUrl m_apiUrl;
    QString m_authToken;
    QString m_writeToken;
    int m_timeout = 30000;
};

}  // namespace GReader

#endif  // SAILRSS_GREADERCLIENT_H
