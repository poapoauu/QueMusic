#include "IPlaybackEngine.h"
#include "IPlaybackPlugin.h"
#include "PlaybackEngineManager.h"

#include <QTest>

class FakePlaybackEngine final : public IPlaybackEngine {
    Q_OBJECT

public:
    using IPlaybackEngine::IPlaybackEngine;

    void open(const StreamDescriptor &source) override { openedSource = source; }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }
    PlaybackCapabilities capabilities() const override { return PlaybackCapability::Audio; }

    StreamDescriptor openedSource;
};

class FakePlaybackPlugin final : public IPlaybackPlugin {
public:
    QString engineId() const override { return QStringLiteral("fake-playback"); }
    QString engineName() const override { return QStringLiteral("Fake Playback"); }
    IPlaybackEngine *createEngine(QObject *parent) override { return new FakePlaybackEngine(parent); }
};

class PlaybackPluginContractTest : public QObject {
    Q_OBJECT

private slots:
    void registersPlaybackPlugin();
    void rejectsDuplicatePlaybackEngineId();
    void forwardsStreamDescriptorWithoutSourceDependency();
};

void PlaybackPluginContractTest::registersPlaybackPlugin()
{
    PlaybackEngineManager manager;
    FakePlaybackPlugin plugin;

    QVERIFY(manager.registerPlugin(&plugin));
    QCOMPARE(manager.engineIds(), QStringList({QStringLiteral("fake-playback")}));
    QVERIFY(manager.useEngine(plugin.engineId()));
    QVERIFY(manager.currentEngine() != nullptr);
}

void PlaybackPluginContractTest::rejectsDuplicatePlaybackEngineId()
{
    PlaybackEngineManager manager;
    FakePlaybackPlugin firstPlugin;
    FakePlaybackPlugin duplicatePlugin;

    QVERIFY(manager.registerPlugin(&firstPlugin));
    QVERIFY(!manager.registerPlugin(&duplicatePlugin));
    QCOMPARE(manager.engineIds(), QStringList({QStringLiteral("fake-playback")}));
}

void PlaybackPluginContractTest::forwardsStreamDescriptorWithoutSourceDependency()
{
    PlaybackEngineManager manager;
    FakePlaybackPlugin plugin;
    const StreamDescriptor source{
        {QStringLiteral("nas"), QStringLiteral("track-42")},
        QUrl(QStringLiteral("https://nas.example.test/stream/track-42")),
        {{QStringLiteral("Authorization"), QStringLiteral("Bearer test-token")}},
        QStringLiteral("audio/flac"),
        {},
        false,
        true,
    };

    QVERIFY(manager.registerPlugin(&plugin));
    QVERIFY(manager.useEngine(plugin.engineId()));

    auto *engine = qobject_cast<FakePlaybackEngine *>(manager.currentEngine());
    QVERIFY(engine != nullptr);
    engine->open(source);

    QCOMPARE(engine->openedSource.track, source.track);
    QCOMPARE(engine->openedSource.url, source.url);
    QCOMPARE(engine->openedSource.headers, source.headers);
    QCOMPARE(engine->openedSource.mimeType, source.mimeType);
    QCOMPARE(engine->openedSource.video, source.video);
    QCOMPARE(engine->openedSource.seekable, source.seekable);
}

QTEST_MAIN(PlaybackPluginContractTest)
#include "tst_PlaybackPluginContract.moc"
