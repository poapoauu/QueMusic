#include "IMusicSourcePlugin.h"

#include <QJsonArray>
#include <QTimer>

class FixtureSourceSession final : public IMusicSourceSession {
    Q_OBJECT

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override { return completeSuccess(QStringLiteral("search")); }
    QUuid browse(const BrowseQuery &) override { return completeUnsupported(); }
    QUuid resolveStream(const TrackRef &) override { return completeUnsupported(); }
    QUuid fetchLyrics(const TrackRef &) override { return completeUnsupported(); }
    void cancel(const QUuid &) override { }

private:
    QUuid completeSuccess(const QString &operation)
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId, operation] {
            emit requestSucceeded(requestId, operation, QJsonArray{});
        });
        return requestId;
    }

    QUuid completeUnsupported()
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId] {
            emit requestFailed(requestId,
                               {SourceErrorKind::Unsupported,
                                QStringLiteral("Fixture operation is unsupported"), std::nullopt});
        });
        return requestId;
    }
};

#if defined(QUEMUSIC_FIXTURE_NOT_SOURCE)

class FixtureNotSourcePlugin final : public QObject {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.quemusic.NotMusicSourcePlugin/1.0")
};

#else

class FixtureSourcePlugin final : public QObject, public IMusicSourcePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID)
    Q_INTERFACES(IMusicSourcePlugin)

public:
    SourceDescriptor descriptor() const override
    {
        return {QStringLiteral(QUEMUSIC_FIXTURE_SOURCE_ID),
                QStringLiteral(QUEMUSIC_FIXTURE_SOURCE_NAME),
                QStringLiteral("1.0.0"),
                QStringLiteral("test"),
                QStringLiteral(QUEMUSIC_FIXTURE_SDK_VERSION),
                SourceCapability::Search};
    }

    bool initialize(SourcePluginContext &) override
    {
#if defined(QUEMUSIC_FIXTURE_INITIALIZE_FAILS)
        return false;
#else
        return true;
#endif
    }

    IMusicSourceSession *createSession(const SourceAccount &, QObject *parent) override
    {
        return new FixtureSourceSession(parent);
    }
};

#endif

#include "FakeSourcePlugin.moc"
