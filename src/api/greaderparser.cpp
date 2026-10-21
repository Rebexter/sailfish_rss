#include "greaderparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace GReader {
namespace Parser {

namespace {

const QString ItemIdPrefix = QStringLiteral("tag:google.com,2005:reader/item/");

std::optional<QJsonObject> parseObject(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("Expected a JSON object");
        return std::nullopt;
    }
    return doc.object();
}

std::optional<QJsonArray> arrayMember(const QJsonObject &object, const QString &key, QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        if (error)
            *error = QStringLiteral("Missing array '%1'").arg(key);
        return std::nullopt;
    }
    return value.toArray();
}

// FreshRSS sends large numbers sometimes as JSON numbers and sometimes as
// strings; doubles cannot hold microsecond timestamps exactly, so read
// strings as integers.
qint64 toInt64(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().toLongLong();
    return static_cast<qint64>(value.toDouble());
}

QString firstHref(const QJsonValue &links)
{
    for (const QJsonValue &link : links.toArray()) {
        const QString href = link.toObject().value(QStringLiteral("href")).toString();
        if (!href.isEmpty())
            return href;
    }
    return QString();
}

Item parseItem(const QJsonObject &object)
{
    Item item;
    item.id = itemIdToDecimal(object.value(QStringLiteral("id")).toString());
    item.title = object.value(QStringLiteral("title")).toString();
    item.author = object.value(QStringLiteral("author")).toString();
    item.crawledUsec = toInt64(object.value(QStringLiteral("timestampUsec")));

    const qint64 published = toInt64(object.value(QStringLiteral("published")));
    if (published > 0)
        item.published = QDateTime::fromMSecsSinceEpoch(published * 1000, Qt::UTC);

    item.url = firstHref(object.value(QStringLiteral("alternate")));
    if (item.url.isEmpty())
        item.url = firstHref(object.value(QStringLiteral("canonical")));

    // FreshRSS sends "summary" in its compatibility mode, "content" otherwise.
    QJsonObject content = object.value(QStringLiteral("summary")).toObject();
    if (content.isEmpty())
        content = object.value(QStringLiteral("content")).toObject();
    item.contentHtml = content.value(QStringLiteral("content")).toString();

    const QJsonObject origin = object.value(QStringLiteral("origin")).toObject();
    item.feedId = origin.value(QStringLiteral("streamId")).toString();
    item.feedTitle = origin.value(QStringLiteral("title")).toString();

    // Besides stream IDs, "categories" holds the article's own tags from the
    // feed (e.g. "Community"); only the stream IDs are of interest.
    for (const QJsonValue &value : object.value(QStringLiteral("categories")).toArray()) {
        const QString category = value.toString();
        if (category == StreamId::Read)
            item.read = true;
        else if (category == StreamId::Starred)
            item.starred = true;
        else if (category.startsWith(StreamId::LabelPrefix))
            item.labels.append(category);
    }

    for (const QJsonValue &value : object.value(QStringLiteral("enclosure")).toArray()) {
        const QJsonObject enclosure = value.toObject();
        const QString href = enclosure.value(QStringLiteral("href")).toString();
        if (href.isEmpty())
            continue;
        item.enclosures.append({href, enclosure.value(QStringLiteral("type")).toString(),
                                toInt64(enclosure.value(QStringLiteral("length")))});
    }

    return item;
}

}  // namespace

QString parseAuthToken(const QByteArray &body)
{
    for (const QByteArray &line : body.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith("Auth="))
            return QString::fromUtf8(trimmed.mid(5));
    }
    return QString();
}

std::optional<QList<Subscription>> parseSubscriptions(const QByteArray &json, QString *error)
{
    const auto object = parseObject(json, error);
    if (!object)
        return std::nullopt;
    const auto array = arrayMember(*object, QStringLiteral("subscriptions"), error);
    if (!array)
        return std::nullopt;

    QList<Subscription> subscriptions;
    for (const QJsonValue &value : *array) {
        const QJsonObject entry = value.toObject();
        Subscription subscription;
        subscription.id = entry.value(QStringLiteral("id")).toString();
        if (subscription.id.isEmpty())
            continue;
        subscription.title = entry.value(QStringLiteral("title")).toString();
        subscription.url = entry.value(QStringLiteral("url")).toString();
        subscription.htmlUrl = entry.value(QStringLiteral("htmlUrl")).toString();
        subscription.iconUrl = entry.value(QStringLiteral("iconUrl")).toString();
        for (const QJsonValue &categoryValue : entry.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject category = categoryValue.toObject();
            subscription.categories.append({category.value(QStringLiteral("id")).toString(),
                                            category.value(QStringLiteral("label")).toString()});
        }
        subscriptions.append(subscription);
    }
    return subscriptions;
}

std::optional<QList<Tag>> parseTags(const QByteArray &json, QString *error)
{
    const auto object = parseObject(json, error);
    if (!object)
        return std::nullopt;
    const auto array = arrayMember(*object, QStringLiteral("tags"), error);
    if (!array)
        return std::nullopt;

    QList<Tag> tags;
    for (const QJsonValue &value : *array) {
        const QJsonObject entry = value.toObject();
        Tag tag;
        tag.id = entry.value(QStringLiteral("id")).toString();
        if (tag.id.isEmpty())
            continue;
        const QString type = entry.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("folder"))
            tag.type = Tag::Type::Folder;
        else if (type == QLatin1String("tag"))
            tag.type = Tag::Type::Label;
        if (entry.contains(QStringLiteral("unread_count")))
            tag.unreadCount = entry.value(QStringLiteral("unread_count")).toInt();
        tags.append(tag);
    }
    return tags;
}

std::optional<QList<UnreadCount>> parseUnreadCounts(const QByteArray &json, QString *error)
{
    const auto object = parseObject(json, error);
    if (!object)
        return std::nullopt;
    const auto array = arrayMember(*object, QStringLiteral("unreadcounts"), error);
    if (!array)
        return std::nullopt;

    QList<UnreadCount> counts;
    for (const QJsonValue &value : *array) {
        const QJsonObject entry = value.toObject();
        UnreadCount count;
        count.id = entry.value(QStringLiteral("id")).toString();
        if (count.id.isEmpty())
            continue;
        count.count = static_cast<int>(toInt64(entry.value(QStringLiteral("count"))));
        count.newestItemUsec = toInt64(entry.value(QStringLiteral("newestItemTimestampUsec")));
        counts.append(count);
    }
    return counts;
}

std::optional<StreamPage> parseStream(const QByteArray &json, QString *error)
{
    const auto object = parseObject(json, error);
    if (!object)
        return std::nullopt;
    const auto array = arrayMember(*object, QStringLiteral("items"), error);
    if (!array)
        return std::nullopt;

    StreamPage page;
    for (const QJsonValue &value : *array) {
        Item item = parseItem(value.toObject());
        if (!item.id.isEmpty())
            page.items.append(item);
    }
    page.continuation = object->value(QStringLiteral("continuation")).toString();
    return page;
}

std::optional<ItemIdsPage> parseItemIds(const QByteArray &json, QString *error)
{
    const auto object = parseObject(json, error);
    if (!object)
        return std::nullopt;
    const auto array = arrayMember(*object, QStringLiteral("itemRefs"), error);
    if (!array)
        return std::nullopt;

    ItemIdsPage page;
    for (const QJsonValue &value : *array) {
        const QString id = itemIdToDecimal(value.toObject().value(QStringLiteral("id")).toString());
        if (!id.isEmpty())
            page.ids.append(id);
    }
    page.continuation = object->value(QStringLiteral("continuation")).toString();
    return page;
}

QString itemIdToDecimal(const QString &id)
{
    bool ok = false;
    if (id.startsWith(ItemIdPrefix)) {
        const qulonglong value = id.mid(ItemIdPrefix.size()).toULongLong(&ok, 16);
        return ok ? QString::number(value) : QString();
    }
    const qulonglong value = id.toULongLong(&ok, 10);
    return ok ? QString::number(value) : QString();
}

}  // namespace Parser
}  // namespace GReader
