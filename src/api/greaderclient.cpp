#include "greaderclient.h"
#include "greaderparser.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace GReader {

namespace {

const QByteArray UserAgent = QByteArrayLiteral("SailRSS/" APP_VERSION);
const char TimedOutProperty[] = "sailrssTimedOut";

// Wraps a ResultCallback so it receives the body parsed with `parse`.
template <typename T>
std::function<void(const Error &, const QByteArray &)> parsed(
    std::optional<T> (*parse)(const QByteArray &, QString *), const Client::ResultCallback<T> &done)
{
    return [parse, done](const Error &error, const QByteArray &body) {
        if (!error.ok()) {
            done(error, T());
            return;
        }
        QString message;
        const std::optional<T> result = parse(body, &message);
        if (!result) {
            done({Error::Type::Parse, 0, message}, T());
            return;
        }
        done(Error(), *result);
    };
}

}  // namespace

QByteArray encodeForm(const Params &params)
{
    QByteArray encoded;
    for (const auto &param : params) {
        if (!encoded.isEmpty())
            encoded += '&';
        encoded += QUrl::toPercentEncoding(param.first);
        encoded += '=';
        encoded += QUrl::toPercentEncoding(param.second);
    }
    return encoded;
}

Client::Client(QObject *parent)
    : QObject(parent)
{
}

QUrl Client::apiUrlFromServerUrl(const QUrl &serverUrl)
{
    QUrl url = serverUrl.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment);
    QString path = url.path();
    if (path.endsWith(QLatin1String("/api/greader.php")))
        return url;

    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    // The web interface lives under /i/, the API overview under /api/.
    if (path.endsWith(QLatin1String("/i")))
        path.chop(2);
    else if (path.endsWith(QLatin1String("/api")))
        path.chop(4);
    url.setPath(path + QLatin1String("/api/greader.php"));
    return url;
}

void Client::setServerUrl(const QUrl &serverUrl)
{
    m_apiUrl = apiUrlFromServerUrl(serverUrl);
    m_writeToken.clear();
}

QUrl Client::apiUrl() const
{
    return m_apiUrl;
}

void Client::setAuthToken(const QString &token)
{
    m_authToken = token;
    m_writeToken.clear();
}

QString Client::authToken() const
{
    return m_authToken;
}

void Client::setTimeout(int milliseconds)
{
    m_timeout = milliseconds;
}

void Client::login(const QString &user, const QString &apiPassword, const Callback &done)
{
    setAuthToken(QString());
    const Params form = {{QStringLiteral("Email"), user}, {QStringLiteral("Passwd"), apiPassword}};
    post(QStringLiteral("/accounts/ClientLogin"), form, [this, done](const Error &error, const QByteArray &body) {
        if (!error.ok()) {
            Error result = error;
            if (result.type == Error::Type::Auth)
                result.message = tr("Wrong user name or API password");
            done(result);
            return;
        }
        const QString token = Parser::parseAuthToken(body);
        if (token.isEmpty()) {
            done({Error::Type::Parse, 0, tr("The server did not return a session token")});
            return;
        }
        setAuthToken(token);
        done(Error());
    });
}

void Client::fetchSubscriptions(const ResultCallback<QList<Subscription>> &done)
{
    get(QStringLiteral("/reader/api/0/subscription/list"), {}, parsed(&Parser::parseSubscriptions, done));
}

void Client::fetchTags(const ResultCallback<QList<Tag>> &done)
{
    get(QStringLiteral("/reader/api/0/tag/list"), {}, parsed(&Parser::parseTags, done));
}

void Client::fetchUnreadCounts(const ResultCallback<QList<UnreadCount>> &done)
{
    get(QStringLiteral("/reader/api/0/unread-count"), {}, parsed(&Parser::parseUnreadCounts, done));
}

void Client::fetchStream(const StreamQuery &query, const ResultCallback<StreamPage> &done)
{
    get(QStringLiteral("/reader/api/0/stream/contents"), streamParams(query), parsed(&Parser::parseStream, done));
}

void Client::fetchItemIds(const StreamQuery &query, const ResultCallback<ItemIdsPage> &done)
{
    get(QStringLiteral("/reader/api/0/stream/items/ids"), streamParams(query), parsed(&Parser::parseItemIds, done));
}

void Client::fetchItems(const QStringList &itemIds, const ResultCallback<StreamPage> &done)
{
    Params form;
    for (const QString &id : itemIds)
        form.append({QStringLiteral("i"), id});
    post(QStringLiteral("/reader/api/0/stream/items/contents"), form, parsed(&Parser::parseStream, done));
}

void Client::editTags(const QStringList &itemIds, const QStringList &add, const QStringList &remove,
                      const Callback &done)
{
    Params form;
    for (const QString &id : itemIds)
        form.append({QStringLiteral("i"), id});
    for (const QString &tag : add)
        form.append({QStringLiteral("a"), tag});
    for (const QString &tag : remove)
        form.append({QStringLiteral("r"), tag});
    postWithToken(QStringLiteral("/reader/api/0/edit-tag"), form, done);
}

void Client::setRead(const QStringList &itemIds, bool read, const Callback &done)
{
    const QStringList tag = {StreamId::Read};
    editTags(itemIds, read ? tag : QStringList(), read ? QStringList() : tag, done);
}

void Client::setStarred(const QStringList &itemIds, bool starred, const Callback &done)
{
    const QStringList tag = {StreamId::Starred};
    editTags(itemIds, starred ? tag : QStringList(), starred ? QStringList() : tag, done);
}

void Client::markAllAsRead(const QString &streamId, qint64 olderThanUsec, const Callback &done)
{
    const Params form = {{QStringLiteral("s"), streamId}, {QStringLiteral("ts"), QString::number(olderThanUsec)}};
    postWithToken(QStringLiteral("/reader/api/0/mark-all-as-read"), form, done);
}

void Client::get(const QString &path, const Params &query, const BodyCallback &done)
{
    Params params = query;
    params.append({QStringLiteral("output"), QStringLiteral("json")});
    send(m_network.get(request(path, params)), done);
}

void Client::post(const QString &path, const Params &form, const BodyCallback &done)
{
    QNetworkRequest req = request(path, {});
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    send(m_network.post(req, encodeForm(form)), done);
}

// Write calls need the separate write token ("T"), fetched once and cached.
void Client::postWithToken(const QString &path, Params form, const Callback &done)
{
    auto withToken = [this, path, form, done]() mutable {
        form.append({QStringLiteral("T"), m_writeToken});
        post(path, form, [this, done](const Error &error, const QByteArray &body) {
            if (!error.ok()) {
                if (error.type == Error::Type::Auth)
                    m_writeToken.clear();
                done(error);
                return;
            }
            if (body.trimmed() != "OK") {
                done({Error::Type::Parse, 0, tr("Unexpected response from the server")});
                return;
            }
            done(Error());
        });
    };

    if (!m_writeToken.isEmpty()) {
        withToken();
        return;
    }
    get(QStringLiteral("/reader/api/0/token"), {}, [this, withToken, done](const Error &error, const QByteArray &body) mutable {
        if (!error.ok()) {
            done(error);
            return;
        }
        m_writeToken = QString::fromUtf8(body.trimmed());
        if (m_writeToken.isEmpty()) {
            done({Error::Type::Parse, 0, tr("The server did not return a write token")});
            return;
        }
        withToken();
    });
}

void Client::send(QNetworkReply *reply, const BodyCallback &done)
{
    QTimer::singleShot(m_timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty(TimedOutProperty, true);
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        Error error;
        if (reply->property(TimedOutProperty).toBool()) {
            error = {Error::Type::Network, 0, tr("The server did not respond in time")};
        } else if (status == 401) {
            error = {Error::Type::Auth, status, tr("Not logged in or session expired")};
        } else if (status == 503) {
            error = {Error::Type::Http, status, tr("The API is unavailable. Is API access enabled in FreshRSS?")};
        } else if (status >= 400) {
            error = {Error::Type::Http, status,
                     tr("Server error %1: %2").arg(status).arg(QString::fromUtf8(body.trimmed().left(200)))};
        } else if (reply->error() != QNetworkReply::NoError) {
            error = {Error::Type::Network, status, reply->errorString()};
        }
        done(error, body);
    });
}

QNetworkRequest Client::request(const QString &path, const Params &query) const
{
    QUrl url = m_apiUrl;
    url.setPath(url.path() + path);
    if (!query.isEmpty())
        url.setQuery(QString::fromLatin1(encodeForm(query)), QUrl::StrictMode);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", UserAgent);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    if (!m_authToken.isEmpty())
        req.setRawHeader("Authorization", "GoogleLogin auth=" + m_authToken.toUtf8());
    return req;
}

Params Client::streamParams(const StreamQuery &query)
{
    Params params = {{QStringLiteral("s"), query.streamId}, {QStringLiteral("n"), QString::number(query.count)}};
    if (!query.continuation.isEmpty())
        params.append({QStringLiteral("c"), query.continuation});
    if (query.newerThan > 0)
        params.append({QStringLiteral("ot"), QString::number(query.newerThan)});
    if (!query.excludeTarget.isEmpty())
        params.append({QStringLiteral("xt"), query.excludeTarget});
    if (!query.includeTarget.isEmpty())
        params.append({QStringLiteral("it"), query.includeTarget});
    if (query.oldestFirst)
        params.append({QStringLiteral("r"), QStringLiteral("o")});
    return params;
}

}  // namespace GReader
