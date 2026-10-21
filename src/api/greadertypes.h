#ifndef SAILRSS_GREADERTYPES_H
#define SAILRSS_GREADERTYPES_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Data returned by the Google Reader compatible API of FreshRSS
// (p/api/greader.php). Stream IDs are kept as the server sends them,
// e.g. "feed/2" or "user/-/label/Sailfish".
namespace GReader {

namespace StreamId {
const QString ReadingList = QStringLiteral("user/-/state/com.google/reading-list");
const QString Read = QStringLiteral("user/-/state/com.google/read");
const QString Unread = QStringLiteral("user/-/state/com.google/unread");
const QString Starred = QStringLiteral("user/-/state/com.google/starred");
const QString LabelPrefix = QStringLiteral("user/-/label/");
}

struct Category {
    QString id;     // "user/-/label/<name>"
    QString label;  // "<name>"
};

struct Subscription {
    QString id;  // "feed/<n>"
    QString title;
    QString url;      // feed URL
    QString htmlUrl;  // website
    QString iconUrl;
    QList<Category> categories;
};

struct Tag {
    enum class Type {
        State,   // built-in streams such as starred or reading-list
        Folder,  // a category that holds feeds
        Label,   // a user label that holds articles
    };
    QString id;
    Type type = Type::State;
    int unreadCount = -1;  // only sent for labels
};

struct UnreadCount {
    QString id;  // feed, category, label or reading-list stream ID
    int count = 0;
    qint64 newestItemUsec = 0;
};

struct Enclosure {
    QString url;
    QString type;
    qint64 length = 0;
};

struct Item {
    QString id;  // decimal entry ID, e.g. "1792608449293848"
    QString feedId;
    QString feedTitle;
    QString title;
    QString author;
    QString url;
    QString contentHtml;
    QDateTime published;
    qint64 crawledUsec = 0;
    bool read = false;
    bool starred = false;
    QStringList labels;  // folder and label stream IDs ("user/-/label/...")
    QList<Enclosure> enclosures;
};

struct StreamPage {
    QList<Item> items;
    QString continuation;  // empty on the last page
};

struct ItemIdsPage {
    QStringList ids;  // decimal entry IDs
    QString continuation;
};

}  // namespace GReader

#endif  // SAILRSS_GREADERTYPES_H
