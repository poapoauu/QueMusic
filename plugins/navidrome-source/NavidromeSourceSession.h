#pragma once

#include "IMusicSourceArtworkSession.h"
#include "IMusicSourceSession.h"

#include <QNetworkAccessManager>
#include <QHash>
#include <QPointer>
#include <QUrlQuery>

class NavidromeSourceSession final : public IMusicSourceSession,
                                     public IMusicSourceArtworkSession {
    Q_OBJECT
    Q_INTERFACES(IMusicSourceArtworkSession)

public:
    NavidromeSourceSession(SourceAccount account, QNetworkAccessManager *network,
                           QObject *parent = nullptr);

    QUuid ping();
    QUuid search(const SearchQuery &query) override;
    QUuid browse(const BrowseQuery &query) override;
    QUuid resolveStream(const TrackRef &track) override;
    QUuid fetchArtwork(const TrackRef &track) override;
    QUuid fetchLyrics(const TrackRef &track) override;
    void cancel(const QUuid &requestId) override;

private:
    struct PendingRequest {
        QUuid requestId;
        QString operation;
        QString stage;
        QPointer<QNetworkReply> reply;
        bool cancelled = false;
    };

    struct TrackMetadata {
        QString artist;
        QString title;
    };

    QUuid startRequest(const QString &operation, const QString &endpoint,
                       QUrlQuery query = {}, QUuid requestId = {});
    bool buildAuthenticatedEndpointUrl(const QString &endpoint, QUrlQuery query, QUrl *url,
                                      SourceError *error) const;
    QUuid scheduleSuccess(const QString &operation, const QJsonValue &result);
    QUuid scheduleFailure(const QString &operation, const SourceError &error);
    void finishSuccess(const PendingRequest &pending, const QJsonValue &result);
    void finishFailure(const PendingRequest &pending, const SourceError &error);
    QUuid completeUnsupported(const QString &operation);

    SourceAccount m_account;
    QPointer<QNetworkAccessManager> m_network;
    QHash<QUuid, PendingRequest> m_pendingRequests;
    QHash<QString, TrackMetadata> m_trackMetadata;
    QHash<QUuid, TrackRef> m_lyricsTracks;
};
