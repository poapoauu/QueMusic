#include "NavidromeSourceSession.h"

#include <QCryptographicHash>
#include <QJsonArray>
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

NavidromeSourceSession::~NavidromeSourceSession()
{
    m_account.secret.fill('\0');
    m_account.secret.clear();
}

QUuid NavidromeSourceSession::ping()
{
    return startRequest(QStringLiteral("ping"), QStringLiteral("ping"));
}

QUuid NavidromeSourceSession::search(const SearchQuery &query)
{
    QUrlQuery parameters;
    const int limit = qMax(0, query.limit);
    parameters.addQueryItem(QStringLiteral("query"), query.query);
    parameters.addQueryItem(QStringLiteral("songCount"), QString::number(limit));
    parameters.addQueryItem(QStringLiteral("albumCount"), QString::number(limit));
    parameters.addQueryItem(QStringLiteral("artistCount"), QString::number(limit));
    return startRequest(QStringLiteral("search"), QStringLiteral("search3"), parameters);
}

QUuid NavidromeSourceSession::browse(const BrowseQuery &query)
{
    QUrlQuery parameters;
    if (query.path.isEmpty()) {
        return startRequest(QStringLiteral("browse"), QStringLiteral("getIndexes"), parameters);
    }

    parameters.addQueryItem(QStringLiteral("id"), query.path);
    return startRequest(QStringLiteral("browse"), QStringLiteral("getMusicDirectory"), parameters);
}

QUuid NavidromeSourceSession::resolveStream(const TrackRef &track)
{
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("id"), track.nativeId);
    QUrl url;
    SourceError error;
    if (!buildAuthenticatedEndpointUrl(QStringLiteral("stream"), parameters, &url, &error)) {
        return scheduleFailure(QStringLiteral("resolveStream"), error);
    }

    return scheduleSuccess(QStringLiteral("resolveStream"),
                           QJsonObject{{QStringLiteral("track"),
                                        QJsonObject{{QStringLiteral("sourceId"), track.sourceId},
                                                    {QStringLiteral("nativeId"), track.nativeId}}},
                                       {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                                       {QStringLiteral("seekable"), true}});
}

QUuid NavidromeSourceSession::fetchArtwork(const TrackRef &track)
{
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("id"), track.nativeId);
    QUrl url;
    SourceError error;
    if (!buildAuthenticatedEndpointUrl(QStringLiteral("getCoverArt"), parameters, &url, &error)) {
        return scheduleFailure(QStringLiteral("fetchArtwork"), error);
    }

    return scheduleSuccess(QStringLiteral("fetchArtwork"),
                           QJsonObject{{QStringLiteral("track"),
                                        QJsonObject{{QStringLiteral("sourceId"), track.sourceId},
                                                    {QStringLiteral("nativeId"), track.nativeId}}},
                                       {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}});
}

QUuid NavidromeSourceSession::fetchLyrics(const TrackRef &track)
{
    const QUuid requestId = QUuid::createUuid();
    m_lyricsTracks.insert(requestId, track);
    const auto metadata = m_trackMetadata.constFind(track.nativeId);
    if (metadata != m_trackMetadata.cend()) {
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("artist"), metadata->artist);
        parameters.addQueryItem(QStringLiteral("title"), metadata->title);
        return startRequest(QStringLiteral("fetchLyrics"), QStringLiteral("getLyrics"), parameters,
                            requestId);
    }

    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("id"), track.nativeId);
    return startRequest(QStringLiteral("fetchLyrics"), QStringLiteral("getSong"), parameters,
                        requestId);
}

void NavidromeSourceSession::cancel(const QUuid &requestId)
{
    auto pending = m_pendingRequests.find(requestId);
    if (pending == m_pendingRequests.end()) {
        return;
    }

    pending->cancelled = true;
    if (pending->operation == QStringLiteral("fetchLyrics")) {
        m_lyricsTracks.remove(requestId);
    }
    const QPointer<QNetworkReply> reply = pending->reply;
    m_pendingRequests.erase(pending);
    if (!reply.isNull()) {
        reply->abort();
        reply->deleteLater();
    }
}

QUuid NavidromeSourceSession::startRequest(const QString &operation, const QString &endpoint,
                                           QUrlQuery query, QUuid requestId)
{
    if (m_network.isNull()) {
        return scheduleFailure(operation,
                               {SourceErrorKind::Unavailable,
                                QStringLiteral("Navidrome network manager is unavailable"),
                                std::nullopt});
    }

    QUrl url;
    SourceError error;
    if (!buildAuthenticatedEndpointUrl(endpoint, query, &url, &error)) {
        return scheduleFailure(operation, error);
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->get(request);
    const QUuid publicRequestId = requestId.isNull() ? QUuid::createUuid() : requestId;
    m_pendingRequests.insert(publicRequestId,
                             {publicRequestId, operation, endpoint, reply, false});

    connect(reply, &QNetworkReply::finished, this, [this, publicRequestId] {
        auto pending = m_pendingRequests.find(publicRequestId);
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
            const int code = response.value(QStringLiteral("error")).toObject()
                                 .value(QStringLiteral("code"))
                                 .toInt();
            SourceErrorKind kind = SourceErrorKind::InvalidRequest;
            if (code == 40 || code == 41) {
                kind = SourceErrorKind::Authentication;
            } else if (code == 50) {
                kind = SourceErrorKind::Authorization;
            } else if (code == 70) {
                kind = SourceErrorKind::NotFound;
            }
            finishFailure(request,
                          {kind,
                           QStringLiteral("Navidrome rejected the request"), status});
            return;
        }

        if (request.operation == QStringLiteral("search")) {
            const QJsonValue searchResultValue = response.value(QStringLiteral("searchResult3"));
            if (!searchResultValue.isObject()) {
                finishFailure(request,
                              {SourceErrorKind::InvalidRequest,
                               QStringLiteral("Navidrome search response is missing searchResult3"), status});
                return;
            }
            const QJsonObject searchResult = searchResultValue.toObject();

            QJsonArray items;
            const auto appendItems = [this, &items](const QJsonArray &source, const QString &kind) {
                for (const QJsonValue &value : source) {
                    const QJsonObject item = value.toObject();
                    QJsonObject normalized{{QStringLiteral("kind"), kind},
                                           {QStringLiteral("id"), item.value(QStringLiteral("id"))},
                                           {QStringLiteral("sourceId"), QStringLiteral("navidrome")}};
                    if (kind == QStringLiteral("track")) {
                        normalized.insert(QStringLiteral("title"), item.value(QStringLiteral("title")));
                        normalized.insert(QStringLiteral("artist"), item.value(QStringLiteral("artist")));
                        normalized.insert(QStringLiteral("album"), item.value(QStringLiteral("album")));
                        normalized.insert(QStringLiteral("duration"), item.value(QStringLiteral("duration")));
                        normalized.insert(QStringLiteral("coverArtId"), item.value(QStringLiteral("coverArt")));
                        const QString artist = item.value(QStringLiteral("artist")).toString();
                        const QString title = item.value(QStringLiteral("title")).toString();
                        if (!artist.isEmpty() && !title.isEmpty()) {
                            m_trackMetadata.insert(item.value(QStringLiteral("id")).toString(),
                                                   {artist, title});
                        }
                    } else {
                        normalized.insert(QStringLiteral("title"), item.value(QStringLiteral("name")));
                        normalized.insert(QStringLiteral("artist"), item.value(QStringLiteral("artist")));
                    }
                    items.append(normalized);
                }
            };
            appendItems(searchResult.value(QStringLiteral("song")).toArray(), QStringLiteral("track"));
            appendItems(searchResult.value(QStringLiteral("album")).toArray(), QStringLiteral("album"));
            appendItems(searchResult.value(QStringLiteral("artist")).toArray(), QStringLiteral("artist"));
            finishSuccess(request, QJsonObject{{QStringLiteral("items"), items}});
            return;
        }

        if (request.operation == QStringLiteral("browse")) {
            QJsonArray items;
            if (request.stage == QStringLiteral("getIndexes")) {
                const QJsonObject indexes = response.value(QStringLiteral("indexes")).toObject();
                if (indexes.isEmpty()) {
                    finishFailure(request,
                                  {SourceErrorKind::InvalidRequest,
                                   QStringLiteral("Navidrome browse response is missing indexes"), status});
                    return;
                }
                for (const QJsonValue &value : indexes.value(QStringLiteral("artist")).toArray()) {
                    const QJsonObject artist = value.toObject();
                    items.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("artist")},
                                             {QStringLiteral("id"), artist.value(QStringLiteral("id"))},
                                             {QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                                             {QStringLiteral("title"), artist.value(QStringLiteral("name"))}});
                }
            } else {
                const QJsonObject directory = response.value(QStringLiteral("directory")).toObject();
                if (directory.isEmpty()) {
                    finishFailure(request,
                                  {SourceErrorKind::InvalidRequest,
                                   QStringLiteral("Navidrome browse response is missing directory"), status});
                    return;
                }
                for (const QJsonValue &value : directory.value(QStringLiteral("child")).toArray()) {
                    const QJsonObject child = value.toObject();
                    const bool directoryChild = child.value(QStringLiteral("isDir")).toBool();
                    items.append(QJsonObject{{QStringLiteral("kind"),
                                              directoryChild ? QStringLiteral("directory")
                                                             : QStringLiteral("track")},
                                             {QStringLiteral("id"), child.value(QStringLiteral("id"))},
                                             {QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                                             {QStringLiteral("title"), child.value(QStringLiteral("title"))},
                                             {QStringLiteral("artist"), child.value(QStringLiteral("artist"))}});
                    if (!directoryChild) {
                        const QString artist = child.value(QStringLiteral("artist")).toString();
                        const QString title = child.value(QStringLiteral("title")).toString();
                        if (!artist.isEmpty() && !title.isEmpty()) {
                            m_trackMetadata.insert(child.value(QStringLiteral("id")).toString(),
                                                   {artist, title});
                        }
                    }
                }
            }
            finishSuccess(request, QJsonObject{{QStringLiteral("items"), items}});
            return;
        }

        if (request.operation == QStringLiteral("fetchLyrics")) {
            const TrackRef track = m_lyricsTracks.value(request.requestId);
            if (request.stage == QStringLiteral("getSong")) {
                const QJsonObject song = response.value(QStringLiteral("song")).toObject();
                const QString artist = song.value(QStringLiteral("artist")).toString();
                const QString title = song.value(QStringLiteral("title")).toString();
                if (song.isEmpty() || artist.isEmpty() || title.isEmpty()) {
                    finishFailure(request,
                                  {SourceErrorKind::InvalidRequest,
                                   QStringLiteral("Navidrome song metadata is incomplete"), status});
                    return;
                }
                m_trackMetadata.insert(track.nativeId, {artist, title});
                QUrlQuery parameters;
                parameters.addQueryItem(QStringLiteral("artist"), artist);
                parameters.addQueryItem(QStringLiteral("title"), title);
                startRequest(QStringLiteral("fetchLyrics"), QStringLiteral("getLyrics"), parameters,
                             request.requestId);
                return;
            }

            const QJsonObject lyrics = response.value(QStringLiteral("lyrics")).toObject();
            if (lyrics.isEmpty()) {
                finishFailure(request,
                              {SourceErrorKind::InvalidRequest,
                               QStringLiteral("Navidrome lyrics response is missing lyrics"), status});
                return;
            }
            m_lyricsTracks.remove(request.requestId);
            finishSuccess(request,
                          QJsonObject{{QStringLiteral("track"),
                                       QJsonObject{{QStringLiteral("sourceId"), track.sourceId},
                                                   {QStringLiteral("nativeId"), track.nativeId}}},
                                      {QStringLiteral("lyrics"), lyrics.value(QStringLiteral("value"))},
                                      {QStringLiteral("synced"), lyrics.value(QStringLiteral("synced"))}});
            return;
        }

        finishSuccess(request, response);
    });

    return publicRequestId;
}

bool NavidromeSourceSession::buildAuthenticatedEndpointUrl(const QString &endpoint, QUrlQuery query,
                                                           QUrl *url, SourceError *error) const
{
    const QUrl serverUrl(m_account.parameters.value(QStringLiteral("serverUrl")).toString());
    const QString username = m_account.parameters.value(QStringLiteral("username")).toString();
    if (!serverUrl.isValid() || serverUrl.host().isEmpty() ||
        (serverUrl.scheme() != QStringLiteral("http") && serverUrl.scheme() != QStringLiteral("https")) ||
        username.isEmpty() || m_account.secret.isEmpty()) {
        *error = {SourceErrorKind::InvalidRequest,
                  QStringLiteral("Navidrome account configuration is incomplete or invalid"),
                  std::nullopt};
        return false;
    }

    *url = serverUrl;
    QString path = url->path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (!path.endsWith(QStringLiteral("/rest"))) {
        path += QStringLiteral("/rest");
    }
    url->setPath(path + QLatin1Char('/') + endpoint + QStringLiteral(".view"));

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
    url->setQuery(query);
    return true;
}

QUuid NavidromeSourceSession::scheduleSuccess(const QString &operation, const QJsonValue &result)
{
    const QUuid requestId = QUuid::createUuid();
    m_pendingRequests.insert(requestId,
                             {requestId, operation, QStringLiteral("local"), nullptr, false});
    QTimer::singleShot(0, this, [this, requestId, result] {
        auto pending = m_pendingRequests.find(requestId);
        if (pending == m_pendingRequests.end() || pending->cancelled) {
            return;
        }

        const PendingRequest request = pending.value();
        m_pendingRequests.erase(pending);
        finishSuccess(request, result);
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
    if (pending.operation == QStringLiteral("fetchLyrics")) {
        m_lyricsTracks.remove(pending.requestId);
    }
    emit requestFailed(pending.requestId, error);
}

QUuid NavidromeSourceSession::completeUnsupported(const QString &operation)
{
    return scheduleFailure(operation,
                           {SourceErrorKind::Unsupported,
                            QStringLiteral("Navidrome operation is not implemented: %1").arg(operation),
                            std::nullopt});
}
