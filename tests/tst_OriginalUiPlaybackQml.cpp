#include <QFile>
#include <QGuiApplication>
#include <QColor>
#include <QTest>
#include <QUuid>
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
    void currentFavoriteRequiresCapabilitiesAndTheMenuPlaybackToken();
    void actualFavoriteButtonShowsChoicesAndRejectsAStaleMenu();
};

class LyricsAdapterDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList currentLyrics MEMBER lines NOTIFY changed)
    Q_PROPERTY(QString currentLyricsState MEMBER state NOTIFY changed)
    Q_PROPERTY(QUrl currentCover MEMBER cover NOTIFY changed)
    Q_PROPERTY(QVariantMap currentFavorite MEMBER favorite NOTIFY changed)
public:
    QVariantList lines;
    QString state = "empty";
    QUrl cover;
    QVariantMap favorite;
    int favoriteCalls = 0;
    bool lastFavorite = false;
    QString lastFavoriteToken;
    Q_INVOKABLE QUuid setCurrentFavorite(bool value, const QString &token) {
        ++favoriteCalls; lastFavorite = value; lastFavoriteToken = token;
        favorite.insert("pending", true); emit changed(); return QUuid::createUuid();
    }
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

void OriginalUiPlaybackQmlTest::currentFavoriteRequiresCapabilitiesAndTheMenuPlaybackToken()
{
    QQmlEngine engine; LyricsAdapterDouble music;
    music.favorite = {{"canFavorite", true}, {"canUnfavorite", true}, {"state", "unknown"},
                      {"pending", false}, {"failed", false}, {"token", "current-A"}};
    QQmlComponent component(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> adapter(component.createWithInitialProperties({
        {"musicAdapter", QVariant::fromValue<QObject *>(&music)}, {"sourceMode", true}, {"sourceActive", true}}));
    QVERIFY2(adapter, qPrintable(component.errorString()));
    QVariant result;
    QVERIFY(adapter->property("favoriteEnabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "toggleFavorite", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toString(), QString("choose")); QCOMPARE(music.favoriteCalls, 0);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, true), Q_ARG(QVariant, "current-A")));
    QVERIFY(result.toBool()); QCOMPARE(music.favoriteCalls, 1); QVERIFY(music.lastFavorite);
    QCOMPARE(music.lastFavoriteToken, QString("current-A")); QVERIFY(!adapter->property("favoriteEnabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_ARG(QVariant, false), Q_ARG(QVariant, "current-A")));
    QCOMPARE(music.favoriteCalls, 1); // Pending double-click rejected.
    music.favorite["pending"] = false; music.favorite["state"] = "favorite"; emit music.changed();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "toggleFavorite", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toString(), QString("submitted")); QCOMPARE(music.favoriteCalls, 2); QVERIFY(!music.lastFavorite);
    music.favorite["pending"] = false; music.favorite["state"] = "unknown"; music.favorite["token"] = "current-B"; emit music.changed();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_ARG(QVariant, true), Q_ARG(QVariant, "current-A")));
    QCOMPARE(music.favoriteCalls, 2); // Open menu from the old occurrence is no longer valid.
    music.favorite["canFavorite"] = false; emit music.changed();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_ARG(QVariant, true), Q_ARG(QVariant, "current-B")));
    QCOMPARE(music.favoriteCalls, 2);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_ARG(QVariant, false), Q_ARG(QVariant, "current-B")));
    QCOMPARE(music.favoriteCalls, 3);
    music.favorite["pending"] = false; music.favorite["canUnfavorite"] = false; emit music.changed();
    QVERIFY(!adapter->property("favoriteEnabled").toBool());
    music.favorite["canFavorite"] = true; emit music.changed();
    QVERIFY(adapter->setProperty("sourceActive", false));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_ARG(QVariant, true), Q_ARG(QVariant, "current-B")));
    QCOMPARE(music.favoriteCalls, 3);
    QVERIFY(adapter->setProperty("sourceActive", true));
    QVERIFY(adapter->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(!adapter->property("favoriteEnabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "toggleFavorite", Q_RETURN_ARG(QVariant, result)));
    QCOMPARE(result.toString(), QString("disabled"));
    QVERIFY(adapter->setProperty("musicAdapter", QVariant::fromValue<QObject *>(&music)));
    QVERIFY(adapter->setProperty("sourceMode", false));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setFavorite", Q_ARG(QVariant, true), Q_ARG(QVariant, "current-B")));
    QCOMPARE(music.favoriteCalls, 3);
}

void OriginalUiPlaybackQmlTest::actualFavoriteButtonShowsChoicesAndRejectsAStaleMenu()
{
    QQmlEngine engine; LyricsAdapterDouble music;
    music.favorite = {{"canFavorite", true}, {"canUnfavorite", true}, {"state", "unknown"},
                      {"pending", false}, {"failed", false}, {"token", "A"}};
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"musicAdapter", QVariant::fromValue<QObject *>(&music)}, {"sourceMode", true}, {"sourceActive", true}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.lastIndexOf("SButton {", source.indexOf("id: likeButton")); QVERIFY(start >= 0);
    int depth = 0, end = start;
    for (; end < source.size(); ++end) {
        if (source[end] == '{') ++depth;
        else if (source[end] == '}' && --depth == 0) { ++end; break; }
    }
    // Run the actual button handlers/bindings with lightweight visual controls;
    // this is not a screenshot or a proof of the real popup rendering.
    const auto qml = QStringLiteral(R"(import QtQuick
Item {
    id: musicControlMin
    property var presentation
    property bool legacyFavorite: false
    property var playListModel: ({playListIndex: -1, count: 0})
    property var styleFixture: ({themes: {themeColor: "#00ff00", textColor: "#222222", hoverColor: "#cccccc"}})
    property QtObject window: QtObject { property bool sourceLyricsMode: true; property var lyricsAdapter: musicControlMin.presentation }
    component SButton: Item { signal clicked(); property string iconCharacter; property real radius; property color buttonColor;
        property color hoverColor; property color iconColor; property bool shadowEnabled; property bool hovered: false }
    component QTip: Item { property string text }
    component QMenu: Item { property list<string> model: []; property bool masked; property var blurSource;
        property int popupCalls: 0; property int closeCalls: 0; signal clicked(int index);
        function popup() { ++popupCalls } function close() { ++closeCalls } }
%1
})").arg(source.mid(start, end - start));
    auto fixture = qml;
    fixture.replace("Style.themes", "musicControlMin.styleFixture.themes");
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/favorite-button-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *button = root->findChild<QObject *>("currentPlaybackFavoriteButton"); QVERIFY(button);
    auto *menu = root->findChild<QObject *>("currentPlaybackFavoriteMenu"); QVERIFY(menu);
    QVERIFY(button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked")); QCOMPARE(music.favoriteCalls, 0);
    QCOMPARE(menu->property("model").toStringList(), QStringList({"收藏", "取消收藏"}));
    QCOMPARE(menu->property("playbackToken").toString(), QString("A"));
    music.favorite["token"] = "B"; emit music.changed();
    QVERIFY(menu->property("closeCalls").toInt() > 0);
    QVERIFY(QMetaObject::invokeMethod(menu, "clicked", Q_ARG(int, 0))); QCOMPARE(music.favoriteCalls, 0);
    music.favorite["canFavorite"] = false; emit music.changed();
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(menu->property("model").toStringList(), QStringList({"取消收藏"}));
    QVERIFY(QMetaObject::invokeMethod(menu, "clicked", Q_ARG(int, 0)));
    QCOMPARE(music.favoriteCalls, 1); QVERIFY(!music.lastFavorite); QCOMPARE(music.lastFavoriteToken, QString("B"));
    QVERIFY(!button->property("enabled").toBool());
    music.favorite["pending"] = false; music.favorite["state"] = "favorite"; emit music.changed();
    QCOMPARE(button->property("iconColor").value<QColor>(), QColor("#00ff00"));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked")); QCOMPARE(music.favoriteCalls, 2);
    QVERIFY(!music.lastFavorite);
    QVERIFY(bridge->setProperty("sourceActive", false)); QVERIFY(!button->property("enabled").toBool());
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
    QVERIFY(controlsText.contains(QStringLiteral("enabled: window.sourceLyricsMode ? window.lyricsAdapter.favoriteEnabled")));
    QVERIFY(controlsText.contains(QStringLiteral("window.lyricsAdapter.setFavorite(values[index], playbackToken)")));
    QVERIFY(controlsText.contains(QStringLiteral("sourceFavoriteMenu.playbackToken = info.token")));
    QVERIFY(!controlsText.contains(QStringLiteral("likeButton.iconColor =")));

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
