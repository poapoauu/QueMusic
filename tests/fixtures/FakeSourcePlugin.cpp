#include "IMusicSourcePlugin.h"

#include <QJsonValue>

class FixtureSourceSession final : public IMusicSourceSession {
    Q_OBJECT

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override { return QUuid::createUuid(); }
    QUuid browse(const BrowseQuery &) override { return QUuid::createUuid(); }
    QUuid resolveStream(const TrackRef &) override { return QUuid::createUuid(); }
    QUuid fetchLyrics(const TrackRef &) override { return QUuid::createUuid(); }
    void cancel(const QUuid &) override { }
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
                SourceCapability::Search | SourceCapability::StreamAudio};
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
