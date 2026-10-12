#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QVector>
#include <cmath>
#include <optional>

// Plugin-private anonymous protocol. No authentication, cache, UI or SDK ABI.
namespace KugouRankProtocol {
enum class Operation { Catalog, Tracks };
struct Entry { QString id, title, subtitle, description; };
struct TrackPage { QJsonArray rows; int total = 0; };

inline QString rankId(const QJsonValue &value)
{
    QString text;
    if (value.isString()) text = value.toString();
    else if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < 1 || number > 2147483647
            || std::floor(number) != number) return {};
        text = QString::number(qint64(number));
    } else return {};
    static const QRegularExpression pattern(QStringLiteral("\\A[1-9][0-9]{0,9}\\z"));
    if (!pattern.match(text).hasMatch() || text.toLongLong() > 2147483647) return {};
    return text;
}

inline std::optional<QNetworkRequest> build(Operation operation, const QString &id = {},
                                           int page = 1, int limit = 20)
{
    QUrl url;
    QUrlQuery query;
    query.addQueryItem("version", "9108"); query.addQueryItem("plat", "0");
    switch (operation) {
    case Operation::Catalog:
        if (!id.isEmpty() || page != 1 || limit != 20) return {};
        url = QUrl(QStringLiteral("https://mobilecdnbj.kugou.com/api/v3/rank/list"));
        query.addQueryItem("withsong", "0");
        break;
    case Operation::Tracks:
        if (rankId(QJsonValue(id)) != id || id.isEmpty()
            || page < 1 || page > 1000000 || limit < 1 || limit > 100) return {};
        url = QUrl(QStringLiteral("https://mobilecdnbj.kugou.com/api/v3/rank/song"));
        query.addQueryItem("ranktype", "2"); query.addQueryItem("area_code", "1");
        query.addQueryItem("with_res_tag", "1"); query.addQueryItem("rankid", id);
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("pagesize", QString::number(limit));
        break;
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

inline bool validEnvelope(const QJsonObject &response)
{
    return response.value("status").isDouble() && response.value("status").toDouble() == 1
        && response.value("data").isObject()
        && response.value("data").toObject().value("info").isArray();
}
inline bool boundedText(const QJsonValue &value, int maximum, bool required = false)
{
    if (value.isUndefined()) return !required;
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.size() > maximum || (required && text.trimmed().isEmpty())) return false;
    for (const auto ch : text) if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
inline std::optional<QVector<Entry>> catalog(const QJsonObject &response)
{
    if (!validEnvelope(response)) return {};
    const auto rows = response.value("data").toObject().value("info").toArray();
    if (rows.size() > 256) return {};
    QVector<Entry> result;
    QSet<QString> ids;
    for (const auto &row : rows) {
        if (!row.isObject()) return {};
        const auto object = row.toObject();
        const auto id = rankId(object.value("rankid"));
        if (id.isEmpty() || ids.contains(id) || !boundedText(object.value("rankname"), 1024, true)
            || !boundedText(object.value("update_frequency"), 1024)
            || !boundedText(object.value("intro"), 8192)) return {};
        ids.insert(id);
        result.append({id, object.value("rankname").toString(),
            object.value("update_frequency").toString(), object.value("intro").toString()});
    }
    // An empty successful catalog stays empty; never manufacture builtin ranks.
    return result;
}
inline std::optional<TrackPage> tracks(const QJsonObject &response, int limit)
{
    if (!validEnvelope(response) || limit < 1 || limit > 100) return {};
    const auto data = response.value("data").toObject();
    const auto count = data.value("total"); const double total = count.toDouble(-1);
    const auto rows = data.value("info").toArray();
    if (!count.isDouble() || !std::isfinite(total) || total < 0 || total > 2147483647
        || std::floor(total) != total || rows.size() > limit || rows.size() > total) return {};
    TrackPage result; result.total = int(total);
    static const QRegularExpression hashPattern(QStringLiteral("\\A[0-9a-fA-F]{32}\\z"));
    for (const auto &row : rows) {
        if (!row.isObject()) return {};
        const auto object = row.toObject();
        if (!hashPattern.match(object.value("hash").toString()).hasMatch()
            || !boundedText(object.value("songname"), 1024, true)
            || !boundedText(object.value("album_name"), 1024)) return {};
        QString artist;
        const auto authors = object.value("authors");
        if (!authors.isUndefined()) {
            if (!authors.isArray() || authors.toArray().size() > 64) return {};
            for (const auto &author : authors.toArray()) {
                if (!author.isObject() || !boundedText(author.toObject().value("author_name"), 1024, true)) return {};
                if (artist.isEmpty()) artist = author.toObject().value("author_name").toString();
            }
        }
        // Whitelist only metadata consumed by track(); no wire dictionaries,
        // alternate hashes, signed URLs or credentials. Unknown pay_type must
        // remain unknown, not inherit the legacy integer default of "free".
        QJsonObject projected{{"hash", object.value("hash")}, {"songname", object.value("songname")},
            {"singername", artist}};
        if (object.contains("album_name")) projected.insert("album_name", object.value("album_name"));
        const auto duration = object.value("duration");
        if (duration.isDouble() && duration.toDouble() >= 0 && duration.toDouble() <= 7 * 86400)
            projected.insert("duration", duration);
        const auto pay = object.value("pay_type");
        if (pay.isDouble()) projected.insert("pay_type", pay);
        result.rows.append(projected);
    }
    return result;
}
}
