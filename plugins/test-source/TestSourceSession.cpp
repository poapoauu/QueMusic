#include "TestSourceSession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

QUuid TestSourceSession::search(const SearchQuery &query)
{
    Q_UNUSED(query);

    const QJsonObject track{{QStringLiteral("sourceId"), QStringLiteral("test-source")},
                            {QStringLiteral("nativeId"), QStringLiteral("test-track-1")}};
    return completeSuccess(QStringLiteral("search"), QJsonArray{track});
}

QUuid TestSourceSession::browse(const BrowseQuery &query)
{
    Q_UNUSED(query);
    return completeUnsupported();
}

QUuid TestSourceSession::resolveStream(const TrackRef &track)
{
    const QJsonObject trackObject{{QStringLiteral("sourceId"), track.sourceId},
                                  {QStringLiteral("nativeId"), track.nativeId}};
    return completeSuccess(QStringLiteral("resolveStream"),
                           QJsonObject{{QStringLiteral("track"), trackObject},
                                       {QStringLiteral("url"),
                                        QStringLiteral("https://example.invalid/%1.mp3")
                                            .arg(track.nativeId)},
                                       {QStringLiteral("mimeType"), QStringLiteral("audio/mpeg")},
                                       {QStringLiteral("video"), false},
                                       {QStringLiteral("seekable"), true}});
}

QUuid TestSourceSession::fetchArtwork(const TrackRef &track)
{
    const QJsonObject trackObject{{QStringLiteral("sourceId"), track.sourceId},
                                  {QStringLiteral("nativeId"), track.nativeId}};
    return completeSuccess(QStringLiteral("fetchArtwork"),
                           QJsonObject{{QStringLiteral("track"), trackObject},
                                       {QStringLiteral("url"),
                                        QStringLiteral("https://example.invalid/%1.png")
                                            .arg(track.nativeId)},
                                       {QStringLiteral("mimeType"), QStringLiteral("image/png")}});
}

QUuid TestSourceSession::fetchLyrics(const TrackRef &track)
{
    Q_UNUSED(track);
    return completeUnsupported();
}

void TestSourceSession::cancel(const QUuid &requestId)
{
    m_cancelledRequests.insert(requestId);
}

QUuid TestSourceSession::completeSuccess(const QString &operation, const QJsonValue &result)
{
    const QUuid requestId = QUuid::createUuid();
    QTimer::singleShot(0, this, [this, requestId, operation, result] {
        if (m_cancelledRequests.remove(requestId)) {
            return;
        }
        emit requestSucceeded(requestId, operation, result);
    });
    return requestId;
}

QUuid TestSourceSession::completeUnsupported()
{
    const QUuid requestId = QUuid::createUuid();
    QTimer::singleShot(0, this, [this, requestId] {
        if (m_cancelledRequests.remove(requestId)) {
            return;
        }
        emit requestFailed(requestId,
                           {SourceErrorKind::Unsupported,
                            QStringLiteral("Operation is not supported by test-source"), std::nullopt});
    });
    return requestId;
}
