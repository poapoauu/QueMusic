#pragma once
#include <QCryptographicHash>
#include <QMap>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <optional>

// Private protocol construction only: no transport, persistence, login grant,
// or Host configuration. Values are based on the existing repository protocol,
// not evidence that the remote service still accepts them.
namespace KugouQrRequests {
struct DeviceIdentity {
    QString dfid;
    QString mid;
};
enum class Operation { CreateCode, PollCode };

inline QByteArray signature(const QMap<QString, QString> &parameters)
{
    QStringList pairs;
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it)
        pairs.append(it.key() + QLatin1Char('=') + it.value());
    pairs.sort();
    const QByteArray salt = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt";
    return QCryptographicHash::hash(salt + pairs.join(QString()).toUtf8() + salt,
                                    QCryptographicHash::Md5).toHex();
}

inline std::optional<QNetworkRequest> build(Operation operation, const DeviceIdentity &identity,
                                           qint64 epochSeconds, const QString &code = {})
{
    static const QRegularExpression dfidPattern(QStringLiteral("\\A[A-Z0-9]{24}\\z"));
    static const QRegularExpression midPattern(QStringLiteral("\\A(?:0|[1-9][0-9]{0,38})\\z"));
    if (!dfidPattern.match(identity.dfid).hasMatch() || !midPattern.match(identity.mid).hasMatch()
        || epochSeconds <= 0 || epochSeconds > 253402300799LL)
        return std::nullopt;
    if (identity.mid.size() == 39 && identity.mid > QStringLiteral("340282366920938463463374607431768211455"))
        return std::nullopt;
    if (operation != Operation::CreateCode && operation != Operation::PollCode)
        return std::nullopt;
    if (operation == Operation::CreateCode && !code.isEmpty()) return std::nullopt;
    if (operation == Operation::PollCode) {
        if (code.isEmpty() || code.size() > 1024) return std::nullopt;
        for (const auto character : code)
            if (character.unicode() < 0x21 || character.unicode() > 0x7e) return std::nullopt;
    }
    const QString timestamp = QString::number(epochSeconds);
    QMap<QString, QString> parameters{{"dfid", identity.dfid}, {"mid", identity.mid}, {"uuid", "-"},
        {"appid", "1001"}, {"clientver", "20489"}, {"clienttime", timestamp},
        {"plat", "4"}, {"srcappid", "2919"}};
    if (operation == Operation::CreateCode) {
        parameters.insert("type", "1");
        parameters.insert("qrcode_txt", "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=1001&");
    } else {
        parameters.insert("qrcode", code);
    }
    parameters.insert("signature", QString::fromLatin1(signature(parameters)));
    QUrlQuery query;
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it)
        // QUrlQuery treats percent triplets as already encoded. Encode literal
        // '%' as well so an opaque key such as "%26" survives round-tripping.
        query.addQueryItem(it.key(), QString::fromLatin1(QUrl::toPercentEncoding(it.value())));
    QUrl url(operation == Operation::CreateCode ? "https://login-user.kugou.com/v2/qrcode"
                                               : "https://login-user.kugou.com/v2/get_userinfo_qrcode");
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("dfid", identity.dfid.toUtf8());
    request.setRawHeader("mid", identity.mid.toUtf8());
    request.setRawHeader("clienttime", timestamp.toUtf8());
    request.setRawHeader("kg-rc", "1");
    request.setRawHeader("kg-thash", "5d816a0");
    request.setRawHeader("kg-rec", "1");
    request.setRawHeader("kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F");
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    return request;
}
}
