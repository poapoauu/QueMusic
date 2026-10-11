#pragma once
#include "KugouQrProtocol.h"
#include <QJsonDocument>
#include <QJsonParseError>

// Private credential payload inside Host-managed secure storage. Not a UI DTO,
// cookie, Source SDK format, or authority to mark an account authenticated.
namespace KugouCredentialRecord {
struct Account {
    KugouQrProtocol::Device device;
    KugouQrProtocol::Credentials credentials;
};
inline bool valid(const Account &account)
{
    const auto &device = account.device;
    if (!KugouQrProtocol::validScope(device.sourceInstanceId, device.accountId)) return false;
    static const QRegularExpression guidPattern(QStringLiteral(
        "\\A[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\\z"));
    if (!guidPattern.match(device.guid).hasMatch()) return false;
    if (device.requestIdentity.mid != KugouQrProtocol::decimalDigest(
            QCryptographicHash::hash(device.guid.toUtf8(), QCryptographicHash::Md5))) return false;
    if (!KugouQrRequests::build(KugouQrRequests::Operation::CreateCode, device.requestIdentity, 1)) return false;
    // Stored IDs must be canonical strings, even when the server sent a safe integer.
    const auto id = KugouQrProtocol::userId(account.credentials.userId);
    return id && *id == account.credentials.userId
        && KugouQrProtocol::opaqueAscii(account.credentials.token, 4096)
        && !account.credentials.token.contains(';');
}
inline std::optional<QByteArray> encode(const Account &account)
{
    if (!valid(account)) return std::nullopt;
    const auto &device = account.device;
    return QJsonDocument(QJsonObject{{"version", 1}, {"pluginPackageId", "org.quemusic.source.kugou"},
        {"sourceId", "kugou"}, {"sourceInstanceId", device.sourceInstanceId}, {"accountId", device.accountId},
        {"guid", device.guid}, {"dfid", device.requestIdentity.dfid}, {"mid", device.requestIdentity.mid},
        {"userId", account.credentials.userId}, {"token", account.credentials.token}}).toJson(QJsonDocument::Compact);
}
inline std::optional<Account> decode(const QByteArray &bytes, const QString &instance, const QString &accountId,
                                     const QString &expectedUserId = {})
{
    if (bytes.isEmpty() || bytes.size() > 16384 || !KugouQrProtocol::validScope(instance, accountId)) return std::nullopt;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto object = document.object();
    const QStringList strings{"accountId", "dfid", "guid", "mid", "pluginPackageId", "sourceId", "sourceInstanceId", "token", "userId"};
    auto keys = strings; keys.append("version"); keys.sort();
    if (object.keys() != keys || !object.value("version").isDouble() || object.value("version").toDouble() != 1)
        return std::nullopt;
    for (const auto &key : strings) if (!object.value(key).isString()) return std::nullopt;
    if (object.value("pluginPackageId").toString() != "org.quemusic.source.kugou"
        || object.value("sourceId").toString() != "kugou"
        || object.value("sourceInstanceId").toString() != instance || object.value("accountId").toString() != accountId)
        return std::nullopt;
    Account account{{instance, accountId, object.value("guid").toString(),
        {object.value("dfid").toString(), object.value("mid").toString()}},
        {object.value("userId").toString(), object.value("token").toString()}};
    if (!valid(account) || (!expectedUserId.isEmpty() && account.credentials.userId != expectedUserId)) return std::nullopt;
    // This private version has one canonical encoding. Reject duplicate keys,
    // alternate encodings and trailing data rather than trusting JSON's last-key wins.
    const auto canonical = encode(account);
    if (!canonical || *canonical != bytes) return std::nullopt;
    return account;
}
}
