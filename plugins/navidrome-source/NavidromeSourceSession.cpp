#include "NavidromeSourceSession.h"

#include <QJsonValue>
#include <QTimer>

#include <optional>

NavidromeSourceSession::NavidromeSourceSession(SourceAccount account,
                                               QNetworkAccessManager *network, QObject *parent)
    : IMusicSourceSession(parent), m_account(std::move(account)), m_network(network)
{
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
    m_cancelledRequests.insert(requestId);
}

QUuid NavidromeSourceSession::completeUnsupported(const QString &operation)
{
    const QUuid requestId = QUuid::createUuid();
    QTimer::singleShot(0, this, [this, requestId, operation] {
        if (m_cancelledRequests.remove(requestId)) {
            return;
        }

        emit requestFailed(requestId,
                           {SourceErrorKind::Unsupported,
                            QStringLiteral("Navidrome operation is not implemented: %1").arg(operation),
                            std::nullopt});
    });
    return requestId;
}
