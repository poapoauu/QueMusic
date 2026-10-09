#include <QFile>
#include <QGuiApplication>
#include <QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include <memory>

class QueueModelDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count CONSTANT)
public:
    explicit QueueModelDouble(int rows) : rows(rows) {}
    int count() const { return rows; }
private:
    int rows = 0;
};

class LegacyPlayerDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing MEMBER playing)
    Q_PROPERTY(qint64 position MEMBER position)
public:
    int refreshCalls = 0;
    bool playing = false;
    qint64 position = 0;
    int playCalls = 0, pauseCalls = 0;
    Q_INVOKABLE void play() { ++playCalls; }
    Q_INVOKABLE void pause() { ++pauseCalls; }
    Q_INVOKABLE void refreshLegacyMusicPlay() { ++refreshCalls; }
};

class CoordinatorDouble final : public QObject {
    Q_OBJECT
public:
    int playQueueCalls = 0;
    int lastIndex = -1;
    Q_INVOKABLE void playQueueEntry(int index)
    {
        ++playQueueCalls;
        lastIndex = index;
    }
};

class PlaybackControllerDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 position MEMBER position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration MEMBER duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing MEMBER playing NOTIFY playingChanged)
    Q_PROPERTY(bool seekable MEMBER seekable NOTIFY seekableChanged)
    Q_PROPERTY(qreal volume MEMBER volume NOTIFY volumeChanged)
    Q_PROPERTY(qreal playbackRate MEMBER playbackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool muted MEMBER muted NOTIFY mutedChanged)
    Q_PROPERTY(int state MEMBER state NOTIFY stateChanged)
public:
    qint64 position = 100;
    qint64 duration = 1000;
    bool playing = false;
    bool seekable = true;
    qreal volume = 1.0;
    qreal playbackRate = 1.0;
    bool muted = false;
    int state = 0;
    int playCalls = 0;
    int pauseCalls = 0;
    int stopCalls = 0;
    qint64 sought = -1;

    Q_INVOKABLE void play() { ++playCalls; }
    Q_INVOKABLE void pause() { ++pauseCalls; }
    Q_INVOKABLE void stop() { ++stopCalls; }
    Q_INVOKABLE void seek(qint64 value) { sought = value; }
    Q_INVOKABLE void setVolume(qreal value) { volume = value; }
    Q_INVOKABLE void setPlaybackRate(qreal value) { playbackRate = value; }
    Q_INVOKABLE void setMuted(bool value) { muted = value; }

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void seekableChanged();
    void volumeChanged();
    void playbackRateChanged();
    void mutedChanged();
    void stateChanged();
    void playbackError(QString messageKey);
};

class OriginalUiPlaybackQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void secureQueueSelectionStopsUsingLegacyPlayer();
    void restoredQueueDoesNotSeizeLegacyPlayback();
    void transportAdapterForwardsOnlyTypedControls();
    void mainWiringKeepsSecurePlaybackBelowTheOriginalUi();
    void sourceLyricsNeverFallBackToLegacyDataOrClock();
    void sourceTransportNeverTouchesLegacyPlayer();
};

class LyricsAdapterDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList currentLyrics MEMBER lines NOTIFY changed)
    Q_PROPERTY(QString currentLyricsState MEMBER state NOTIFY changed)
    Q_PROPERTY(QUrl currentCover MEMBER cover NOTIFY changed)
public:
    QVariantList lines;
    QString state = "empty";
    QUrl cover;
    int retries = 0;
    Q_INVOKABLE void retryCurrentLyrics() { ++retries; state = "loading"; emit changed(); }
signals:
    void changed();
};

void OriginalUiPlaybackQmlTest::sourceLyricsNeverFallBackToLegacyDataOrClock()
{
    QQmlEngine engine; LyricsAdapterDouble lyrics; PlaybackControllerDouble controls;
    QQmlComponent component(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QVariantList legacy{QVariantMap{{"time", 0}, {"text", "Legacy lyric"}}};
    std::unique_ptr<QObject> adapter(component.createWithInitialProperties({
        {"musicAdapter", QVariant::fromValue<QObject *>(&lyrics)}, {"controls", QVariant::fromValue<QObject *>(&controls)},
        {"sourceMode", true}, {"sourceActive", true}, {"legacyLines", legacy},
        {"legacyTranslations", QVariantList{"Legacy translation"}}, {"legacyPosition", 900},
        {"legacyDuration", 9000}, {"legacyPlaying", true}, {"legacyActive", true},
        {"legacyCover", "https://legacy.invalid/cover"}}));
    QVERIFY2(adapter, qPrintable(component.errorString()));
    const auto fallback = adapter->property("defaultCover").toString();
    QCOMPARE(adapter->property("cover").toString(), fallback);
    lyrics.cover = QUrl::fromLocalFile("/fixture/cache/cover.png"); emit lyrics.changed();
    QCOMPARE(adapter->property("cover").toString(), lyrics.cover.toString());
    QVERIFY(adapter->property("lines").toList().isEmpty());
    QVERIFY(adapter->property("translations").toList().isEmpty());
    QCOMPARE(adapter->property("position").toLongLong(), controls.position);
    QCOMPARE(adapter->property("duration").toLongLong(), controls.duration);
    QVERIFY(!adapter->property("playing").toBool());
    lyrics.lines = {QVariantMap{{"time", 1000}, {"text", "<img src='https://private.invalid/'>"}}};
    lyrics.state = "ready"; emit lyrics.changed();
    QCOMPARE(adapter->property("lines").toList(), lyrics.lines);
    controls.position = 1200; emit controls.positionChanged();
    QCOMPARE(adapter->property("position").toLongLong(), 1200);
    lyrics.lines.clear(); lyrics.state = "failed"; emit lyrics.changed();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "retry"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "retry"));
    QCOMPARE(lyrics.retries, 1);
    lyrics.lines = {QVariantMap{{"time", 0}, {"text", "Stopped stale lyric"}}}; emit lyrics.changed();
    QVERIFY(adapter->setProperty("sourceActive", false));
    QCOMPARE(adapter->property("cover").toString(), fallback);
    QCOMPARE(adapter->property("position").toLongLong(), 0);
    QVERIFY(adapter->property("lines").toList().isEmpty());
    QVERIFY(!adapter->property("active").toBool());
    QVERIFY(adapter->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(adapter->setProperty("sourceActive", true));
    QCOMPARE(adapter->property("cover").toString(), fallback);
    QVERIFY(adapter->property("lines").toList().isEmpty());
    QVERIFY(adapter->property("translations").toList().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "retry")); QCOMPARE(lyrics.retries, 1);
    QVERIFY(adapter->setProperty("sourceMode", false)); // Only Host explicitly enters legacy mode.
    QCOMPARE(adapter->property("cover").toString(), QString("https://legacy.invalid/cover"));
    QCOMPARE(adapter->property("lines").toList(), legacy);
    QCOMPARE(adapter->property("position").toLongLong(), 900);
    QVERIFY(adapter->property("playing").toBool());
}

void OriginalUiPlaybackQmlTest::sourceTransportNeverTouchesLegacyPlayer()
{
    QQmlEngine engine; PlaybackControllerDouble controls; LegacyPlayerDouble legacy;
    QQmlComponent component(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> adapter(component.createWithInitialProperties({
        {"controls", QVariant::fromValue<QObject *>(&controls)},
        {"legacyPlayer", QVariant::fromValue<QObject *>(&legacy)},
        {"sourceMode", true}, {"sourceActive", true}, {"legacyActive", true}}));
    QVERIFY2(adapter, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback")); QCOMPARE(controls.playCalls, 1);
    controls.playing = true; emit controls.playingChanged();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback")); QCOMPARE(controls.pauseCalls, 1);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 640))); QCOMPARE(controls.sought, 640);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, -1))); QCOMPARE(controls.sought, 640);
    controls.seekable = false; emit controls.seekableChanged();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 200))); QCOMPARE(controls.sought, 640);
    QVERIFY(adapter->setProperty("sourceActive", false));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback"));
    QCOMPARE(controls.playCalls, 1); QCOMPARE(controls.pauseCalls, 1);
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.pauseCalls, 0); QCOMPARE(legacy.position, 0);
    QVERIFY(adapter->setProperty("controls", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(adapter->setProperty("sourceActive", true));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 300)));
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.position, 0);
    QVERIFY(adapter->setProperty("sourceMode", false));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback")); QCOMPARE(legacy.playCalls, 1);
    legacy.playing = true;
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback")); QCOMPARE(legacy.pauseCalls, 1);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 500))); QCOMPARE(legacy.position, 500);
    QCOMPARE(controls.sought, 640);
}

static std::unique_ptr<QObject> createQueueController(QQmlEngine &engine,
                                                       QueueModelDouble *legacyQueue,
                                                       QueueModelDouble *secureQueue,
                                                       LegacyPlayerDouble *legacy,
                                                       CoordinatorDouble *coordinator)
{
    QQmlComponent component(&engine,
        QUrl(QStringLiteral("qrc:/QueMusic/components/LegacyQueueController.qml")));
    if (component.status() != QQmlComponent::Ready)
        return {};
    return std::unique_ptr<QObject>(component.createWithInitialProperties({
        {QStringLiteral("queueModel"), QVariant::fromValue(legacyQueue)},
        {QStringLiteral("secureQueueModel"), QVariant::fromValue(secureQueue)},
        {QStringLiteral("legacyPlayer"), QVariant::fromValue(legacy)},
        {QStringLiteral("playbackCoordinator"), QVariant::fromValue(coordinator)},
        {QStringLiteral("useCoordinator"), true}
    }));
}

static std::unique_ptr<QObject> createPlaybackAdapter(QQmlEngine &engine,
                                                       PlaybackControllerDouble *controller)
{
    QQmlComponent component(&engine,
        QUrl(QStringLiteral("qrc:/QueMusic/components/PlaybackControlsAdapter.qml")));
    if (component.status() != QQmlComponent::Ready)
        return {};
    return std::unique_ptr<QObject>(component.createWithInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(controller)}
    }));
}

void OriginalUiPlaybackQmlTest::secureQueueSelectionStopsUsingLegacyPlayer()
{
    QQmlEngine engine;
    QueueModelDouble legacyQueue(0);
    QueueModelDouble secureQueue(2);
    LegacyPlayerDouble legacy;
    CoordinatorDouble coordinator;
    const auto controller = createQueueController(engine, &legacyQueue, &secureQueue,
                                                  &legacy, &coordinator);
    QVERIFY(controller);

    QVERIFY(QMetaObject::invokeMethod(controller.get(), "playQueueEntry",
                                      Q_ARG(QVariant, 1)));
    QCOMPARE(coordinator.playQueueCalls, 1);
    QCOMPARE(coordinator.lastIndex, 1);
    QCOMPARE(legacy.refreshCalls, 0);
}

void OriginalUiPlaybackQmlTest::restoredQueueDoesNotSeizeLegacyPlayback()
{
    QQmlEngine engine;
    QueueModelDouble legacyQueue(1);
    QueueModelDouble restoredQueue(1);
    LegacyPlayerDouble legacy;
    CoordinatorDouble coordinator;
    const auto controller = createQueueController(engine, &legacyQueue, &restoredQueue,
                                                  &legacy, &coordinator);
    QVERIFY(controller);
    QVERIFY(controller->setProperty("useCoordinator", false));
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "playQueueEntry", Q_ARG(QVariant, 0)));
    QCOMPARE(legacy.refreshCalls, 1);
    QCOMPARE(coordinator.playQueueCalls, 0);
    QVERIFY(controller->setProperty("useCoordinator", true));
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "playQueueEntry", Q_ARG(QVariant, 0)));
    QCOMPARE(coordinator.playQueueCalls, 1);
}

void OriginalUiPlaybackQmlTest::transportAdapterForwardsOnlyTypedControls()
{
    QQmlEngine engine;
    PlaybackControllerDouble controller;
    const auto adapter = createPlaybackAdapter(engine, &controller);
    QVERIFY(adapter);

    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "play"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "pause"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "stop"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 640)));
    QCOMPARE(controller.playCalls, 1);
    QCOMPARE(controller.pauseCalls, 1);
    QCOMPARE(controller.stopCalls, 1);
    QCOMPARE(controller.sought, 640);
    QCOMPARE(adapter->property("position").toLongLong(), 100);
    QCOMPARE(adapter->property("seekable").toBool(), true);
}

void OriginalUiPlaybackQmlTest::mainWiringKeepsSecurePlaybackBelowTheOriginalUi()
{
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source = QString::fromUtf8(file.readAll());
    QVERIFY(source.contains(QStringLiteral("PlaybackControlsAdapter")));
    QVERIFY(source.contains(QStringLiteral("property var playbackAdapter: securePlaybackControls")));
    QVERIFY(source.contains(QStringLiteral("property bool securePlaybackActive")));
    QVERIFY(source.contains(QStringLiteral("property bool secureQueueAvailable")));
    QVERIFY(source.contains(QStringLiteral("playbackCoordinator.currentIndex >= 0")));
    QVERIFY(source.contains(QStringLiteral("queueHistoryStore.warningKey")));
    QVERIFY(source.contains(QStringLiteral("queueHistoryStore.retrySave()")));
    QVERIFY(source.contains(QStringLiteral("window.togglePlayback()")));
    QVERIFY(source.contains(QStringLiteral("LegacyQueueController")));
    QVERIFY(source.contains(QStringLiteral("PlaybackLyricsAdapter")));
    QVERIFY(source.contains(QStringLiteral("sourceMode: window.sourceLyricsMode")));
    QVERIFY(source.contains(QStringLiteral("window.sourceLyricsMode = false")));
    QVERIFY(source.contains(QStringLiteral("readonly property string currentCover: playbackLyrics.cover")));
    QVERIFY(source.contains(QStringLiteral("picWatch.dialog(window.currentCover")));
    QVERIFY(source.contains(QStringLiteral("if (window.sourceLyricsMode) colorExtractor.extractColorsFromUrl(window.currentCover)")));
    for (const auto &path : {QStringLiteral("/layout/PlayerMaxCenter.qml"), QStringLiteral("/components/DesktopPlayerWindow.qml")}) {
        QFile coverFile(QStringLiteral(QUEMUSIC_SOURCE_DIR) + path);
        QVERIFY(coverFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const auto text = QString::fromUtf8(coverFile.readAll());
        QVERIFY(text.contains(QStringLiteral("source: window.currentCover")));
        QVERIFY(!text.contains(QStringLiteral("mainMedia.urlStr")));
        if (path.endsWith(QStringLiteral("DesktopPlayerWindow.qml"))) {
            QVERIFY(!text.contains(QStringLiteral("mainMedia.")));
            QVERIFY(text.contains(QStringLiteral("onClicked: window.togglePlayback()")));
            QVERIFY(text.contains(QStringLiteral("onMoved: window.lyricsAdapter.seek(value)")));
            QVERIFY(text.contains(QStringLiteral("enabled: window.lyricsAdapter.seekable")));
        }
    }
    QVERIFY(source.contains(QStringLiteral("playbackLyrics.togglePlayback()")));
    QFile controlsFile(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml"));
    QVERIFY(controlsFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto controlsText = QString::fromUtf8(controlsFile.readAll());
    QCOMPARE(controlsText.count(QStringLiteral("if (window.sourceLyricsMode && !window.securePlaybackCurrent)")), 3);
    QVERIFY(controlsText.contains(QStringLiteral("onMoved: window.lyricsAdapter.seek(value)")));

    for (const QString &path : {QStringLiteral("/layout/PlayerMaxCenter.qml"), QStringLiteral("/components/DesktopLyrics.qml")}) {
        QFile lyricsFile(QStringLiteral(QUEMUSIC_SOURCE_DIR) + path);
        QVERIFY(lyricsFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const auto text = QString::fromUtf8(lyricsFile.readAll());
        QVERIFY(!text.contains(QStringLiteral("MusicApi.lyrics")));
        QVERIFY(text.contains(QStringLiteral("textFormat: Text.PlainText")));
        QVERIFY(text.contains(QStringLiteral("window.lyricsAdapter")));
        QVERIFY(!text.contains(QStringLiteral("mainMedia.position")));
    }

    QFile queueFile(QStringLiteral(QUEMUSIC_SOURCE_DIR "/components/LegacyQueueController.qml"));
    QVERIFY(queueFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString queueSource = QString::fromUtf8(queueFile.readAll());
    QVERIFY(queueSource.contains(QStringLiteral("playbackCoordinator.playQueueEntry")));

    QFile popupFile(QStringLiteral(QUEMUSIC_SOURCE_DIR "/components/PlayList.qml"));
    QVERIFY(popupFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString popupSource = QString::fromUtf8(popupFile.readAll());
    QVERIFY(popupSource.contains(QStringLiteral("playbackCoordinator.playQueueEntry(index)")));
    QVERIFY(popupSource.contains(QStringLiteral("playbackCoordinator.removeOccurrence")));

    QFile startupFile(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.cpp"));
    QVERIFY(startupFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString startupSource = QString::fromUtf8(startupFile.readAll());
    QVERIFY(startupSource.contains(QStringLiteral("queueHistoryStore.loadAndAttach()")));
    QVERIFY(startupSource.contains(QStringLiteral("setContextProperty(QStringLiteral(\"queueHistoryStore\")")));
}

QTEST_MAIN(OriginalUiPlaybackQmlTest)
#include "tst_OriginalUiPlaybackQml.moc"
