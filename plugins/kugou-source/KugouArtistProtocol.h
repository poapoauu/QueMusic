#pragma once
#include "KugouRankProtocol.h"
#include <climits>

// Private protocol foundation only. Artist navigation is not published until
// the Host's standard Artist -> Albums contract can represent track browsing.
namespace KugouArtistProtocol {
enum class Operation { Popular, Tracks };
struct Entry { QString id, name; };
struct Catalog { QVector<Entry> entries; std::optional<int> total; };
struct TrackPage { QJsonArray rows; std::optional<int> total; };

inline QString artistId(const QJsonValue &value)
{
    QString text;
    if (value.isString()) text = value.toString();
    else if (value.isDouble()) {
        const auto number = value.toDouble();
        if (!std::isfinite(number) || number < 1 || number > 9007199254740991.0
            || std::floor(number) != number) return {};
        text = QString::number(qint64(number));
    } else return {};
    static const QRegularExpression pattern(QStringLiteral("\\A[1-9][0-9]{0,18}\\z"));
    bool ok = false; text.toLongLong(&ok);
    return pattern.match(text).hasMatch() && ok ? text : QString();
}
inline std::optional<QNetworkRequest> build(Operation operation, const QString &id = {},
                                           int page = 1, int limit = 20)
{
    if (page < 1 || page > 1000000 || limit < 1 || limit > 100) return {};
    QUrl url;
    QUrlQuery query;
    query.addQueryItem("version", "9108"); query.addQueryItem("plat", "0");
    query.addQueryItem("page", QString::number(page)); query.addQueryItem("pagesize", QString::number(limit));
    switch (operation) {
    case Operation::Popular:
        if (!id.isEmpty()) return {};
        url = QUrl(QStringLiteral("https://mobilecdnbj.kugou.com/api/v3/singer/rank"));
        query.addQueryItem("type", "1"); break;
    case Operation::Tracks:
        if (id.isEmpty() || artistId(QJsonValue(id)) != id) return {};
        url = QUrl(QStringLiteral("https://mobilecdnbj.kugou.com/api/v3/singer/song"));
        query.addQueryItem("singerid", id); break;
    default: return {};
    }
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Referer", "https://www.kugou.com/");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    return request;
}
struct WirePage { QJsonArray rows; std::optional<int> total; };
inline std::optional<WirePage> wirePage(const QJsonObject &response, const char *primary, int limit)
{
    if (limit < 1 || limit > 100 || !response.value("status").isDouble()
        || response.value("status").toDouble() != 1 || !response.value("data").isObject()) return {};
    const auto data = response.value("data").toObject();
    const auto named = data.value(primary), legacy = data.value("info");
    if (named.isUndefined() && legacy.isUndefined()) return {};
    if ((!named.isUndefined() && !named.isArray()) || (!legacy.isUndefined() && !legacy.isArray())
        || (!named.isUndefined() && !legacy.isUndefined() && named != legacy)) return {};
    WirePage result{named.isUndefined() ? legacy.toArray() : named.toArray(), {}};
    if (result.rows.size() > limit) return {};
    const auto count = data.value("total");
    if (!count.isUndefined()) {
        const auto number = count.toDouble(-1);
        if (!count.isDouble() || !std::isfinite(number) || number < result.rows.size()
            || number < 0 || number > INT_MAX || std::floor(number) != number) return {};
        result.total = int(number);
    }
    return result;
}
inline std::optional<Catalog> catalog(const QJsonObject &response, int limit)
{
    const auto wire = wirePage(response, "singers", limit); if (!wire) return {};
    Catalog result{{}, wire->total}; QSet<QString> ids;
    for (const auto &value : wire->rows) {
        if (!value.isObject()) return {};
        const auto row = value.toObject(); const auto id = artistId(row.value("singerid"));
        if (id.isEmpty() || ids.contains(id) || !KugouRankProtocol::boundedText(row.value("singername"), 1024, true)) return {};
        ids.insert(id); result.entries.append({id, row.value("singername").toString()});
    }
    return result;
}
inline std::optional<TrackPage> tracks(const QJsonObject &response, int limit)
{
    const auto wire = wirePage(response, "songs", limit); if (!wire) return {};
    QJsonArray rows;
    for (const auto &value : wire->rows) {
        if (!value.isObject()) return {};
        const auto row = value.toObject();
        QString title, artist;
        if (!row.value("songname").isUndefined()) {
            if (!KugouRankProtocol::boundedText(row.value("songname"), 1024, true)) return {};
            title = row.value("songname").toString();
        }
        if (!row.value("singername").isUndefined()) {
            if (!KugouRankProtocol::boundedText(row.value("singername"), 1024)) return {};
            artist = row.value("singername").toString();
        }
        if (title.isEmpty()) {
            if (!KugouRankProtocol::boundedText(row.value("filename"), 2048, true)) return {};
            const auto filename = row.value("filename").toString();
            const int split = filename.indexOf('-');
            title = (split < 0 ? filename : filename.mid(split + 1)).trimmed();
            if (artist.isEmpty() && split >= 0) artist = filename.left(split).trimmed();
        }
        QJsonObject projected{{"hash", row.value("hash")}, {"songname", title}};
        if (!artist.isEmpty()) projected.insert("authors", QJsonArray{QJsonObject{{"author_name", artist}}});
        for (const auto *key : {"album_name", "duration", "pay_type"})
            if (row.contains(key)) projected.insert(key, row.value(key));
        rows.append(projected);
    }
    // Reuse the strict hash/metadata/rights whitelist, not legacy toInt() defaults.
    const QJsonObject normalized{{"status", 1}, {"data", QJsonObject{
        {"info", rows}, {"total", wire->total.value_or(int(rows.size()))}}}};
    const auto mapped = KugouRankProtocol::tracks(normalized, limit); if (!mapped) return {};
    return TrackPage{mapped->rows, wire->total}; // absent total stays absent
}
}
