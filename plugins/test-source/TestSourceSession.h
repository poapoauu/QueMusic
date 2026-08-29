#pragma once

#include "IMusicSourceArtworkSession.h"
#include "IMusicSourceSession.h"

#include <QSet>

class QJsonValue;

class TestSourceSession final : public IMusicSourceSession,
                                public IMusicSourceArtworkSession {
    Q_OBJECT
    Q_INTERFACES(IMusicSourceArtworkSession)

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &query) override;
    QUuid browse(const BrowseQuery &query) override;
    QUuid resolveStream(const TrackRef &track) override;
    QUuid fetchArtwork(const TrackRef &track) override;
    QUuid fetchLyrics(const TrackRef &track) override;
    void cancel(const QUuid &requestId) override;

private:
    QUuid completeSuccess(const QString &operation, const QJsonValue &result);
    QUuid completeUnsupported();

    QSet<QUuid> m_cancelledRequests;
};
