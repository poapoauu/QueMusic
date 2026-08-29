#include "IMusicSourcePlugin.h"

#include <QJsonObject>
#include <QSet>
#include <QTimer>

class FrozenV1SourceSession final : public IMusicSourceSession {
    Q_OBJECT

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override { return completeUnsupported(); }
    QUuid browse(const BrowseQuery &) override { return completeUnsupported(); }
    QUuid resolveStream(const TrackRef &) override { return completeUnsupported(); }
    QUuid fetchArtwork(const TrackRef &track) override
    {
        return completeSuccess(QStringLiteral("fetchArtwork"), track.nativeId);
    }
    QUuid fetchLyrics(const TrackRef &track) override
    {
        return completeSuccess(QStringLiteral("fetchLyrics"),
                               QJsonObject{{QStringLiteral("nativeId"), track.nativeId},
                                           {QStringLiteral("text"), QStringLiteral("frozen-v1")}});
    }
    void cancel(const QUuid &requestId) override
    {
        m_cancelledRequests.insert(requestId);
    }

private:
    QUuid completeSuccess(const QString &operation, const QJsonValue &result)
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(20, this, [this, requestId, operation, result] {
            if (!m_cancelledRequests.remove(requestId)) {
                emit requestSucceeded(requestId, operation, result);
            }
        });
        return requestId;
    }

    QUuid completeUnsupported()
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId] {
            if (!m_cancelledRequests.remove(requestId)) {
                emit requestFailed(requestId,
                                   {SourceErrorKind::Unsupported,
                                    QStringLiteral("Frozen v1 fixture operation is unsupported"),
                                    std::nullopt});
            }
        });
        return requestId;
    }

    QSet<QUuid> m_cancelledRequests;
};

class FrozenV1SourcePlugin final : public QObject, public IMusicSourcePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID)
    Q_INTERFACES(IMusicSourcePlugin)

public:
    SourceDescriptor descriptor() const override
    {
        return {QStringLiteral("fixture.frozen-v1"), QStringLiteral("Frozen v1 Source"),
                QStringLiteral("1.0.0"), QStringLiteral("frozen-v1"),
                QStringLiteral("1.0"), SourceCapability::Artwork | SourceCapability::Lyrics};
    }

    bool initialize(SourcePluginContext &) override { return true; }

    IMusicSourceSession *createSession(const SourceAccount &, QObject *parent) override
    {
        return new FrozenV1SourceSession(parent);
    }
};

#include "FrozenV1SourcePlugin.moc"
