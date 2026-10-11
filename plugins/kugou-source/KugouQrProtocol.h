#pragma once
#include "KugouQrRequests.h"
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUuid>
#include <array>
#include <cmath>

// Plugin-private, pure protocol foundations. No QObject/QML exposure, account
// store, cookie construction, transport, or authority to mark a Source ready.
namespace KugouQrProtocol {
inline bool opaqueAscii(const QString &value, int maximum)
{
    if (value.isEmpty() || value.size() > maximum) return false;
    for (const auto c : value)
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) return false;
    return true;
}

inline QString decimalDigest(const QByteArray &digest)
{
    if (digest.size() != 16) return {};
    std::array<int, 39> digits{}; // decimal, least significant digit first
    for (const auto byte : digest) {
        int carry = static_cast<unsigned char>(byte);
        for (auto &digit : digits) {
            const int value = digit * 256 + carry;
            digit = value % 10; carry = value / 10;
        }
    }
    QString result;
    for (auto it = digits.crbegin(); it != digits.crend(); ++it)
        if (!result.isEmpty() || *it) result.append(QChar('0' + *it));
    return result.isEmpty() ? QStringLiteral("0") : result;
}

struct Device {
    QString sourceInstanceId;
    QString accountId;
    QString guid;
    KugouQrRequests::DeviceIdentity requestIdentity;
};
inline std::optional<Device> createDevice(const QString &instance, const QString &account)
{
    if (account.trimmed().isEmpty() || account.size() > 512 || instance != "kugou/" + account)
        return std::nullopt;
    for (const auto c : account) if (c.isNull() || c.isSpace() || c.category() == QChar::Other_Control)
        return std::nullopt;
    Device device;
    device.sourceInstanceId = instance; device.accountId = account;
    device.guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 24; ++i)
        device.requestIdentity.dfid.append(alphabet[QRandomGenerator::system()->bounded(36)]);
    device.requestIdentity.mid = decimalDigest(QCryptographicHash::hash(device.guid.toUtf8(), QCryptographicHash::Md5));
    return device;
}

inline std::optional<QJsonObject> successfulData(const QJsonObject &response)
{
    const auto status = response.value("status");
    if (!status.isDouble() || status.toDouble() != 1 || !response.value("data").isObject())
        return std::nullopt;
    return response.value("data").toObject();
}
struct Code {
    QString key;
    QUrl landingUrl;
};
inline std::optional<Code> mapCode(const QJsonObject &response)
{
    const auto data = successfulData(response);
    if (!data || !data->value("qrcode").isString()) return std::nullopt;
    const auto key = data->value("qrcode").toString();
    if (!opaqueAscii(key, 1024)) return std::nullopt;
    QUrl url("https://h5.kugou.com/apps/loginQRCode/html/index.html");
    QUrlQuery query;
    query.addQueryItem("appid", "1001");
    query.addQueryItem("qrcode", QString::fromLatin1(QUrl::toPercentEncoding(key)));
    url.setQuery(query);
    return Code{key, url};
}

inline std::optional<QString> userId(const QJsonValue &value)
{
    QString id;
    if (value.isString()) id = value.toString();
    else if (value.isDouble()) {
        const auto number = value.toDouble();
        if (!std::isfinite(number) || number < 1 || number > 9007199254740991.0 || std::floor(number) != number)
            return std::nullopt;
        id = QString::number(static_cast<quint64>(number));
    } else return std::nullopt;
    static const QRegularExpression pattern(QStringLiteral("\\A[1-9][0-9]{0,19}\\z"));
    if (!pattern.match(id).hasMatch()) return std::nullopt;
    return id;
}
enum class PollState { Expired, Waiting, Scanned, Confirmed };
// Keep credentials in typed plugin-private data; never register these as Qt
// metatypes or copy them into UI DTOs, error strings, logs, or cookies.
struct Credentials { QString userId; QString token; };
struct Poll {
    PollState state;
    std::optional<Credentials> credentials;
};
inline std::optional<Poll> mapPoll(const QJsonObject &response)
{
    const auto data = successfulData(response);
    if (!data || !data->value("status").isDouble()) return std::nullopt;
    const double status = data->value("status").toDouble();
    if (status == 0) return Poll{PollState::Expired, std::nullopt};
    if (status == 1) return Poll{PollState::Waiting, std::nullopt};
    if (status == 2) return Poll{PollState::Scanned, std::nullopt};
    if (status != 4 || !data->value("token").isString()) return std::nullopt;
    const auto token = data->value("token").toString();
    if (!opaqueAscii(token, 4096) || token.contains(';')) return std::nullopt;
    const bool modern = data->contains("user_id"), legacy = data->contains("userid");
    if (!modern && !legacy) return std::nullopt;
    const auto id = userId(data->value(modern ? "user_id" : "userid"));
    if (!id) return std::nullopt;
    if (modern && legacy) {
        const auto alias = userId(data->value("userid"));
        if (!alias || *alias != *id) return std::nullopt;
    }
    return Poll{PollState::Confirmed, Credentials{*id, token}};
}
}
