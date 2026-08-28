#pragma once

#include "IMusicSourceArtworkSession.h"
#include "IMusicSourceSession.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>

class NavidromeSourceSession final : public IMusicSourceSession,
                                     public IMusicSourceArtworkSession {
    Q_OBJECT
    Q_INTERFACES(IMusicSourceArtworkSession)

public:
    NavidromeSourceSession(SourceAccount account, QNetworkAccessManager *network,
                           QObject *parent = nullptr);

    QUuid search(const SearchQuery &query) override;
    QUuid browse(const BrowseQuery &query) override;
    QUuid resolveStream(const TrackRef &track) override;
    QUuid fetchArtwork(const TrackRef &track) override;
    QUuid fetchLyrics(const TrackRef &track) override;
    void cancel(const QUuid &requestId) override;

private:
    QUuid completeUnsupported(const QString &operation);

    SourceAccount m_account;
    QPointer<QNetworkAccessManager> m_network;
    QSet<QUuid> m_cancelledRequests;
};
