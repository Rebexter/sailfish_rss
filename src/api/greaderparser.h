#ifndef SAILRSS_GREADERPARSER_H
#define SAILRSS_GREADERPARSER_H

#include "greadertypes.h"

#include <QByteArray>
#include <optional>

// Parsers for FreshRSS Google Reader API responses. They do no I/O, so
// they can be tested against recorded responses. On failure they return
// std::nullopt and, if given, set *error to a short description.
namespace GReader {
namespace Parser {

// Body of /accounts/ClientLogin: lines "SID=...", "LSID=...", "Auth=...".
// Returns the Auth value, or an empty string if there is none.
QString parseAuthToken(const QByteArray &body);

std::optional<QList<Subscription>> parseSubscriptions(const QByteArray &json, QString *error = nullptr);
std::optional<QList<Tag>> parseTags(const QByteArray &json, QString *error = nullptr);
std::optional<QList<UnreadCount>> parseUnreadCounts(const QByteArray &json, QString *error = nullptr);
std::optional<StreamPage> parseStream(const QByteArray &json, QString *error = nullptr);
std::optional<ItemIdsPage> parseItemIds(const QByteArray &json, QString *error = nullptr);

// Item IDs come as "tag:google.com,2005:reader/item/<16 hex digits>" in
// stream contents and as decimal in stream/items/ids. Returns the decimal
// form, or an empty string if the ID is not valid.
QString itemIdToDecimal(const QString &id);

}  // namespace Parser
}  // namespace GReader

#endif  // SAILRSS_GREADERPARSER_H
