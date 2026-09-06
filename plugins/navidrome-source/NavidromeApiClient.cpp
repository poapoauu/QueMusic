#include "NavidromeApiClient.h"

#include "v2/SourceSecretsV2.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace {

QString randomSalt()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
}

SourceErrorV2 error(SourceErrorKindV2 kind, const QString &messageKey,
                    const QString &detail, std::optional<int> status = std::nullopt)
{
    return {kind, messageKey, detail, status, kind == SourceErrorKindV2::Network};
}

SourceErrorV2 subsonicFailure(int code, int status)
{
    const std::optional<int> httpStatus = status > 0 ? std::optional<int>(status) : std::nullopt;
    if (code == 40 || code == 41)
        return error(SourceErrorKindV2::Authentication,
                     QStringLiteral("source.authentication.required"),
                     QStringLiteral("Navidrome authentication failed."), httpStatus);
    if (code == 50)
        return error(SourceErrorKindV2::Authorization,
                     QStringLiteral("source.authorization.denied"),
                     QStringLiteral("Navidrome denied this operation."), httpStatus);
    if (code == 70)
        return error(SourceErrorKindV2::NotFound, QStringLiteral("source.notFound"),
                     QStringLiteral("The requested Navidrome resource was not found."), httpStatus);
    return error(SourceErrorKindV2::InvalidRequest, QStringLiteral("source.response.rejected"),
                 QStringLiteral("Navidrome rejected the request."), httpStatus);
}

} // namespace

NavidromeApiClient::NavidromeApiClient(SourceConfigurationV2 configuration,
                                       QNetworkAccessManager *network, QObject *parent)
    : NavidromeApiClient(std::move(configuration), network, randomSalt, parent)
{
}

NavidromeApiClient::NavidromeApiClient(SourceConfigurationV2 configuration,
                                       QNetworkAccessManager *network,
                                       SaltGenerator saltGenerator, QObject *parent)
    : QObject(parent), m_configuration(std::move(configuration)), m_network(network),
      m_saltGenerator(std::move(saltGenerator))
{
}

NavidromeApiClient::~NavidromeApiClient()
{
    const QList<QUuid> ids = m_pending.keys();
    for (const QUuid &id : ids)
        cancel(id);
    m_configuration.secret.fill('\0');
    m_configuration.secret.clear();
}

QUuid NavidromeApiClient::get(const QString &operation, const QString &endpoint, QUrlQuery query)
{
    const QUuid requestId = QUuid::createUuid();
    QUrl url;
    SourceErrorV2 validationError;
    if (!authenticatedUrl(endpoint, std::move(query), &url, &validationError)) {
        m_pending.insert(requestId, {operation, nullptr});
        scheduleFailure(requestId, validationError);
        return requestId;
    }
    if (m_network.isNull()) {
        m_pending.insert(requestId, {operation, nullptr});
        scheduleFailure(requestId,
                        error(SourceErrorKindV2::Unavailable,
                              QStringLiteral("source.network.unavailable"),
                              QStringLiteral("Navidrome networking is unavailable.")));
        return requestId;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->get(request);
    if (reply == nullptr) {
        m_pending.insert(requestId, {operation, nullptr});
        scheduleFailure(requestId,
                        error(SourceErrorKindV2::Network,
                              QStringLiteral("source.network.failed"),
                              QStringLiteral("The Navidrome request could not be started.")));
        return requestId;
    }
    m_pending.insert(requestId, {operation, reply});
    connect(reply, &QNetworkReply::finished, this,
            [this, requestId] { finishReply(requestId); });
    connect(reply, &QObject::destroyed, this, [this, requestId] {
        auto pending = m_pending.find(requestId);
        if (pending == m_pending.end())
            return;
        const SourceErrorV2 networkError = error(
            SourceErrorKindV2::Network, QStringLiteral("source.network.failed"),
            QStringLiteral("The Navidrome request ended unexpectedly."));
        const QString operation = pending->operation;
        m_pending.erase(pending);
        Q_UNUSED(operation)
        emit failed(requestId, networkError);
    });
    if (reply->isFinished())
        QTimer::singleShot(0, this, [this, requestId] { finishReply(requestId); });
    return requestId;
}

void NavidromeApiClient::cancel(const QUuid &requestId)
{
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end())
        return;
    QPointer<QNetworkReply> reply = pending->reply;
    m_pending.erase(pending);
    if (reply) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        delete reply;
    }
}

bool NavidromeApiClient::authenticatedUrl(const QString &endpoint, QUrlQuery query,
                                          QUrl *url, SourceErrorV2 *validationError)
{
    const QVariant serverValue = m_configuration.parameters.value(QStringLiteral("serverUrl"));
    const QVariant usernameValue = m_configuration.parameters.value(QStringLiteral("username"));
    const QUrl serverUrl(serverValue.toString());
    const QString username = usernameValue.toString();
    if (serverValue.metaType().id() != QMetaType::QString
        || usernameValue.metaType().id() != QMetaType::QString
        || !serverUrl.isValid() || serverUrl.host().isEmpty()
        || !serverUrl.userInfo().isEmpty()
        || (serverUrl.scheme() != QStringLiteral("http")
            && serverUrl.scheme() != QStringLiteral("https"))
        || username.isEmpty() || endpoint.isEmpty() || endpoint.contains(QLatin1Char('/'))) {
        *validationError = error(SourceErrorKindV2::InvalidRequest,
                                 QStringLiteral("source.configuration.invalid"),
                                 QStringLiteral("Navidrome connection settings are invalid."));
        return false;
    }

    QByteArray password;
    switch (sourceSecretInputKindV2(m_configuration.secret)) {
    case SourceSecretInputKindV2::LegacyRaw:
        password = m_configuration.secret;
        break;
    case SourceSecretInputKindV2::NamedEnvelope: {
        const auto secrets = decodeSourceSecretsV2(m_configuration.secret);
        if (secrets && secrets->contains(QStringLiteral("password")))
            password = secrets->value(QStringLiteral("password"));
        else
            password.clear();
        if (!secrets || !secrets->contains(QStringLiteral("password")) || password.isEmpty()) {
            password.fill('\0');
            *validationError = error(SourceErrorKindV2::Authentication,
                                     QStringLiteral("source.authentication.invalidCredentials"),
                                     QStringLiteral("Navidrome credentials are unavailable."));
            return false;
        }
        break;
    }
    case SourceSecretInputKindV2::MalformedEnvelope:
        *validationError = error(SourceErrorKindV2::Authentication,
                                 QStringLiteral("source.authentication.invalidCredentials"),
                                 QStringLiteral("Navidrome credentials are unavailable."));
        return false;
    }
    if (m_configuration.secret.isEmpty()) {
        *validationError = error(SourceErrorKindV2::Authentication,
                                 QStringLiteral("source.authentication.invalidCredentials"),
                                 QStringLiteral("Navidrome credentials are unavailable."));
        return false;
    }

    *url = serverUrl;
    url->setQuery(QString());
    url->setFragment({});
    QString path = url->path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (!path.endsWith(QStringLiteral("/rest")))
        path += QStringLiteral("/rest");
    url->setPath(path + QLatin1Char('/') + endpoint + QStringLiteral(".view"));

    const QString salt = m_saltGenerator ? m_saltGenerator() : QString();
    if (salt.isEmpty()) {
        password.fill('\0');
        *validationError = error(SourceErrorKindV2::Unavailable,
                                 QStringLiteral("source.authentication.unavailable"),
                                 QStringLiteral("Navidrome authentication is unavailable."));
        return false;
    }
    const QByteArray token = QCryptographicHash::hash(password + salt.toUtf8(),
                                                       QCryptographicHash::Md5).toHex();
    password.fill('\0');
    for (const QString &reserved : {QStringLiteral("u"), QStringLiteral("p"),
                                    QStringLiteral("t"), QStringLiteral("s"),
                                    QStringLiteral("v"), QStringLiteral("c"),
                                    QStringLiteral("f")})
        query.removeAllQueryItems(reserved);
    query.addQueryItem(QStringLiteral("u"), username);
    query.addQueryItem(QStringLiteral("t"), QString::fromLatin1(token));
    query.addQueryItem(QStringLiteral("s"), salt);
    query.addQueryItem(QStringLiteral("v"), QStringLiteral("1.16.1"));
    query.addQueryItem(QStringLiteral("c"), QStringLiteral("QueMusic"));
    query.addQueryItem(QStringLiteral("f"), QStringLiteral("json"));
    url->setQuery(query);
    return true;
}

void NavidromeApiClient::finishReply(const QUuid &requestId)
{
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end() || pending->reply.isNull())
        return;
    const PendingRequest request = pending.value();
    m_pending.erase(pending);
    QNetworkReply *reply = request.reply;
    disconnect(reply, nullptr, this, nullptr);
    const int rawStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const std::optional<int> status = rawStatus > 0 ? std::optional<int>(rawStatus) : std::nullopt;
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkCode = reply->error();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonValue responseValue = document.object().value(QStringLiteral("subsonic-response"));
    const QJsonObject response = responseValue.toObject();
    if (parseError.error == QJsonParseError::NoError && responseValue.isObject()
        && response.value(QStringLiteral("status")).toString() == QStringLiteral("failed")) {
        emit failed(requestId,
                    subsonicFailure(response.value(QStringLiteral("error")).toObject()
                                        .value(QStringLiteral("code")).toInt(-1),
                                    rawStatus));
        return;
    }
    if (rawStatus == 404 || rawStatus == 405 || rawStatus == 501) {
        emit failed(requestId,
                    error(SourceErrorKindV2::Unsupported,
                          QStringLiteral("source.endpoint.unsupported"),
                          QStringLiteral("This Navidrome endpoint is unavailable."), status));
        return;
    }
    if (rawStatus == 401) {
        emit failed(requestId,
                    error(SourceErrorKindV2::Authentication,
                          QStringLiteral("source.authentication.required"),
                          QStringLiteral("Navidrome authentication failed."), status));
        return;
    }
    if (rawStatus == 403) {
        emit failed(requestId,
                    error(SourceErrorKindV2::Authorization,
                          QStringLiteral("source.authorization.denied"),
                          QStringLiteral("Navidrome denied this operation."), status));
        return;
    }
    if (networkCode != QNetworkReply::NoError || rawStatus < 200 || rawStatus >= 300) {
        emit failed(requestId,
                    error(SourceErrorKindV2::Network, QStringLiteral("source.network.failed"),
                          QStringLiteral("The Navidrome request failed."), status));
        return;
    }
    if (parseError.error != QJsonParseError::NoError || !responseValue.isObject()
        || response.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
        emit failed(requestId,
                    error(SourceErrorKindV2::InvalidRequest,
                          QStringLiteral("source.response.invalid"),
                          QStringLiteral("Navidrome returned an invalid response."), status));
        return;
    }
    emit succeeded(requestId, request.operation, response);
}

void NavidromeApiClient::scheduleFailure(const QUuid &requestId, const SourceErrorV2 &failure)
{
    QTimer::singleShot(0, this, [this, requestId, failure] {
        auto pending = m_pending.find(requestId);
        if (pending == m_pending.end())
            return;
        m_pending.erase(pending);
        emit failed(requestId, failure);
    });
}
