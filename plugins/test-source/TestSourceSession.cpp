#include "TestSourceSession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

QUuid TestSourceSession::search(const SearchQuery &query)
{
    Q_UNUSED(query);

    const QUuid requestId = QUuid::createUuid();
    QTimer::singleShot(0, this, [this, requestId] {
        if (m_cancelledRequests.remove(requestId)) {
            return;
        }

        const QJsonObject track{{QStringLiteral("sourceId"), QStringLiteral("test-source")},
                                {QStringLiteral("nativeId"), QStringLiteral("test-track-1")}};
        emit requestSucceeded(requestId, QStringLiteral("search"), QJsonArray{track});
    });
    return requestId;
}

QUuid TestSourceSession::browse(const BrowseQuery &query)
{
    Q_UNUSED(query);
    return QUuid::createUuid();
}

QUuid TestSourceSession::resolveStream(const TrackRef &track)
{
    Q_UNUSED(track);
    return QUuid::createUuid();
}

QUuid TestSourceSession::fetchLyrics(const TrackRef &track)
{
    Q_UNUSED(track);
    return QUuid::createUuid();
}

void TestSourceSession::cancel(const QUuid &requestId)
{
    m_cancelledRequests.insert(requestId);
}
