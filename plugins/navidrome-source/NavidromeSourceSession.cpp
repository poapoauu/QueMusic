#include "NavidromeSourceSession.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <utility>

#include <optional>

NavidromeSourceSession::NavidromeSourceSession(SourceAccount account,
                                               QNetworkAccessManager *network, QObject *parent)
    : IMusicSourceSession(parent), m_account(std::move(account)), m_network(network)
{
}

QUuid NavidromeSourceSession::ping()
{
    return startRequest(QStringLiteral("ping"), QStringLiteral("ping"));
}

QUuid NavidromeSourceSession::search(const SearchQuery &query)
{
    Q_UNUSED(query);
    return completeUnsupported(QStringLiteral("search"));
}

QUuid NavidromeSourceSession::browse(const BrowseQuery &query)
{
    Q_UNUSED(query);
    return completeUnsupported(QStringLiteral("browse"));
}

QUuid NavidromeSourceSession::resolveStream(const TrackRef &track)
{
    Q_UNUSED(track);
    return completeUnsupported(QStringLiteral("resolveStream"));
}

QUuid NavidromeSourceSession::fetchArtwork(const TrackRef &track)
{
    Q_UNUSED(track);
    return completeUnsupported(QStringLiteral("fetchArtwork"));
}

QUuid NavidromeSourceSession::fetchLyrics(const TrackRef &track)
{
    Q_UNUSED(track);
    return completeUnsupported(QStringLiteral("fetchLyrics"));
}

void NavidromeSourceSession::cancel(const QUuid &requestId)
{
    auto pending = m_pendingRequests.find(requestId);
    if (pending == m_pendingRequests.end()) {
        return;
    }

    pending->cancelled = true;
    const QPointer<QNetworkReply> reply = pending->reply;
    m_pendingRequests.erase(pending);
    if (!reply.isNull()) {
        reply->abort();
        reply->deleteLater();
    }
}

QUuid NavidromeSourceSession::startRequest(const QString &operation, const QString &endpoint,
                                           QUrlQuery query)
{
    if (m_network.isNull()) {
        return scheduleFailure(operation,
                               {SourceErrorKind::Unavailable,
                                QStringLiteral("Navidrome network manager is unavailable"),
                                std::nullopt});
    }

    const QUrl serverUrl(m_account.parameters.value(QStringLiteral("serverUrl")).toString());
    const QString username = m_account.parameters.value(QStringLiteral("username")).toString();
    if (!serverUrl.isValid() || serverUrl.host().isEmpty() ||
        (serverUrl.scheme() != QStringLiteral("http") && serverUrl.scheme() != QStringLiteral("https")) ||
        username.isEmpty() || m_account.secret.isEmpty()) {
        return scheduleFailure(operation,
                               {SourceErrorKind::InvalidRequest,
                                QStringLiteral("Navidrome account configuration is incomplete or invalid"),
                                std::nullopt});
    }

    QUrl url = serverUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (!path.endsWith(QStringLiteral("/rest"))) {
        path += QStringLiteral("/rest");
    }
    url.setPath(path + QLatin1Char('/') + endpoint + QStringLiteral(".view"));

    const QString salt = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
    const QByteArray token = QCryptographicHash::hash(m_account.secret + salt.toUtf8(),
                                                       QCryptographicHash::Md5)
                                 .toHex();
    query.addQueryItem(QStringLiteral("u"), username);
    query.addQueryItem(QStringLiteral("t"), QString::fromLatin1(token));
    query.addQueryItem(QStringLiteral("s"), salt);
    query.addQueryItem(QStringLiteral("v"), QStringLiteral("1.16.1"));
    query.addQueryItem(QStringLiteral("c"), QStringLiteral("QueMusic"));
    query.addQueryItem(QStringLiteral("f"), QStringLiteral("json"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->get(request);
    const QUuid requestId = QUuid::createUuid();
    m_pendingRequests.insert(requestId,
                             {requestId, operation, endpoint, reply, false});

    connect(reply, &QNetworkReply::finished, this, [this, requestId] {
        auto pending = m_pendingRequests.find(requestId);
        if (pending == m_pendingRequests.end()) {
            return;
        }

        const PendingRequest request = pending.value();
        m_pendingRequests.erase(pending);
        if (request.cancelled || request.reply.isNull()) {
            return;
        }

        QNetworkReply *reply = request.reply;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            reply->deleteLater();
            finishFailure(request,
                          {SourceErrorKind::Network,
                           QStringLiteral("Navidrome request failed"),
                           status > 0 ? std::optional<int>(status) : std::nullopt});
            return;
        }
        reply->deleteLater();

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        const QJsonObject response = document.object().value(QStringLiteral("subsonic-response")).toObject();
        if (parseError.error != QJsonParseError::NoError || response.isEmpty()) {
            finishFailure(request,
                          {SourceErrorKind::InvalidRequest,
                           QStringLiteral("Navidrome returned an invalid JSON response"), status});
            return;
        }
        if (response.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
            finishFailure(request,
                          {SourceErrorKind::InvalidRequest,
                           QStringLiteral("Navidrome rejected the request"), status});
            return;
        }

        finishSuccess(request, response);
    });

    return requestId;
}

QUuid NavidromeSourceSession::scheduleFailure(const QString &operation, const SourceError &error)
{
    const QUuid requestId = QUuid::createUuid();
    m_pendingRequests.insert(requestId,
                             {requestId, operation, QStringLiteral("validation"), nullptr, false});
    QTimer::singleShot(0, this, [this, requestId, error] {
        auto pending = m_pendingRequests.find(requestId);
        if (pending == m_pendingRequests.end() || pending->cancelled) {
            return;
        }

        const PendingRequest request = pending.value();
        m_pendingRequests.erase(pending);
        finishFailure(request, error);
    });
    return requestId;
}

void NavidromeSourceSession::finishSuccess(const PendingRequest &pending, const QJsonValue &result)
{
    emit requestSucceeded(pending.requestId, pending.operation, result);
}

void NavidromeSourceSession::finishFailure(const PendingRequest &pending, const SourceError &error)
{
    emit requestFailed(pending.requestId, error);
}

QUuid NavidromeSourceSession::completeUnsupported(const QString &operation)
{
    return scheduleFailure(operation,
                           {SourceErrorKind::Unsupported,
                            QStringLiteral("Navidrome operation is not implemented: %1").arg(operation),
                            std::nullopt});
}
