#pragma once

#include "IMusicSourceSession.h"

#include <QSet>

class TestSourceSession final : public IMusicSourceSession {
    Q_OBJECT

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &query) override;
    QUuid browse(const BrowseQuery &query) override;
    QUuid resolveStream(const TrackRef &track) override;
    QUuid fetchLyrics(const TrackRef &track) override;
    void cancel(const QUuid &requestId) override;

private:
    QSet<QUuid> m_cancelledRequests;
};
