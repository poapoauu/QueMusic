#include <QFile>
#include <QGuiApplication>
#include <QColor>
#include <QTest>
#include <QUuid>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include <memory>
#include <limits>
#include "../cpp/WindowsSmtcManager.h"
#include "../core/playback/QtPlaybackController.h"

namespace {
QString capturedQmlBlock(const QString &source, qsizetype start)
{
    if (start < 0) return {};
    int depth = 0;
    for (auto end = start; end < source.size(); ++end) {
        if (source[end] == '{') ++depth;
        else if (source[end] == '}' && --depth == 0) return source.mid(start, end + 1 - start);
    }
    return {};
}
}

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
    Q_PROPERTY(qreal playbackRate MEMBER playbackRate)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay)
public:
    bool autoPlay() const { ++autoPlayReads; return autoPlayValue; }
    void setAutoPlay(bool value) { ++autoPlayWrites; autoPlayValue = value; }
    mutable int autoPlayReads = 0;
    int autoPlayWrites = 0;
    bool autoPlayValue = true;
    qreal playbackRate = 1;
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
    Q_PROPERTY(QVariantList wavePath MEMBER wavePath NOTIFY wavePathChanged)
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
    int rateCalls = 0;
    QVariantList wavePath;
    qint64 sought = -1;

    Q_INVOKABLE void play() { ++playCalls; }
    Q_INVOKABLE void pause() { ++pauseCalls; }
    Q_INVOKABLE void stop() { ++stopCalls; }
    Q_INVOKABLE void seek(qint64 value) { sought = value; }
    Q_INVOKABLE void setVolume(qreal value) { volume = value; }
    Q_INVOKABLE void setPlaybackRate(qreal value) { ++rateCalls; playbackRate = value; emit playbackRateChanged(); }
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
    void wavePathChanged();
};

class LegacyShutdownDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count)
    Q_PROPERTY(int playListIndex READ index)
    Q_PROPERTY(QString urlStr READ cover)
public:
    int rows = 1, currentIndex = 0, stops = 0;
    mutable int reads = 0;
    int count() const { ++reads; return rows; }
    int index() const { ++reads; return currentIndex; }
    QString cover() const { ++reads; return "file:///legacy-cover.png"; }
    Q_INVOKABLE QVariantMap get(int) { ++reads; return {{"path", "legacy-id"}, {"source", 1}}; }
    Q_INVOKABLE void stop() { ++stops; }
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
    void smtcUsesSourceStateAndIgnoresLegacyEvents();
    void actualInfoDialogUsesOnlyCurrentDisplayFields();
    void actualDownloadButtonNeverFallsBackAfterSourceStops();
    void actualSourceDownloadDialogKeepsTheCapturedPlaybackToken();
    void actualPlayerOptionsNeverConfigureTheLegacyPlayerInSourceMode();
    void spectrumBridgeRejectsLegacyFramesInStickySourceMode();
    void actualSpectrumBindingControlsCoreFromOriginalDisplayPreference();
    void actualDesktopSpotUsesSafePlaybackPresentation();
    void originalProgressRangesFollowPresentationChanges_data();
    void originalProgressRangesFollowPresentationChanges();
    void actualPlayerMetadataMenusUseSafeDisplaySnapshots();
    void actualMenuLabelsTreatPluginTextAsPlainText();
    void remainingPlaybackCaptionsUseSafePresentation_data();
    void remainingPlaybackCaptionsUseSafePresentation();
    void actualSettingsNavigationPreservesPluginAndInstanceSelection();
    void sourceShutdownDoesNotMixLegacyPersistence();
    void coverDialogUsesSafeCurrentLabelsAndFileNames();
    void coverSaveKeepsTheCapturedDestination();
    void coverSaveRejectsFailuresAndChangedImages();
};

class LyricsAdapterDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList currentLyrics MEMBER lines NOTIFY changed)
    Q_PROPERTY(QString currentLyricsState MEMBER state NOTIFY changed)
    Q_PROPERTY(QUrl currentCover MEMBER cover NOTIFY changed)
    Q_PROPERTY(QVariantMap currentFavorite MEMBER favorite NOTIFY changed)
    Q_PROPERTY(QVariantMap currentDownload MEMBER download NOTIFY changed)
public:
    QVariantList lines;
    QString state = "empty";
    QUrl cover;
    QVariantMap favorite;
    int favoriteCalls = 0;
    bool lastFavorite = false;
    QString lastFavoriteToken;
    QVariantMap download;
    int downloadCalls = 0;
    QUrl lastDestination;
    QString lastDownloadToken;
    Q_INVOKABLE QUuid downloadCurrent(const QUrl &destination, const QString &token) {
        ++downloadCalls; lastDestination = destination; lastDownloadToken = token;
        download.insert("pending", true); emit changed(); return QUuid::createUuid();
    }
    Q_INVOKABLE QUuid setCurrentFavorite(bool value, const QString &token) {
        ++favoriteCalls; lastFavorite = value; lastFavoriteToken = token;
        favorite.insert("pending", true); emit changed(); return QUuid::createUuid();
    }
    int retries = 0;
    Q_INVOKABLE void retryCurrentLyrics() { ++retries; state = "loading"; emit changed(); }
signals:
    void changed();
};

void OriginalUiPlaybackQmlTest::actualPlayerOptionsNeverConfigureTheLegacyPlayerInSourceMode()
{
    QQmlEngine engine; PlaybackControllerDouble controls; LegacyPlayerDouble legacy;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    QVERIFY2(bridgeComponent.isReady(), qPrintable(bridgeComponent.errorString()));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"controls", QVariant::fromValue<QObject *>(&controls)}, {"legacyPlayer", QVariant::fromValue<QObject *>(&legacy)},
        {"sourceMode", true}, {"sourceActive", false}})); QVERIFY(bridge);
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.lastIndexOf("QOptionDialog {", source.indexOf("id: playerOptionDialog")); QVERIFY(start >= 0);
    int depth = 0; auto end = start;
    for (; end < source.size(); ++end) {
        if (source[end] == '{') ++depth;
        else if (source[end] == '}' && --depth == 0) { ++end; break; }
    }
    auto dialog = source.mid(start, end - start);
    dialog.replace("Style.", "musicControlMin.styleFixture.");
    dialog.replace("Options.", "musicControlMin.optionsFixture.");
    dialog.replace("mainMedia.", "musicControlMin.legacyPlayer.");
    dialog.replace("musicDevices.", "musicControlMin.devicesFixture.");
    const auto fixture = QStringLiteral(R"(import QtQuick
Item {
    id: musicControlMin
    property var presentation
    property var legacyPlayer
    property int playerRateIndex: 2
    readonly property real currentPlaybackRate: presentation.playbackRate
    property var styleFixture: ({settings: {labelRadius: 6}})
    property var optionsFixture: ({settings: {soundQuality: 2, useDefaultDevice: false, audioDevice: 1}})
    property var devicesFixture: ({audioOutputs: ["default", "custom"]})
    property QtObject window: QtObject {
        property var lyricsAdapter: musicControlMin.presentation
        property bool sourceLyricsMode: musicControlMin.presentation.sourceMode
    }
    component QOptionDialog: Item {
        property string title; property real dialogContentHeight; property alias options: optionHost.data
        Item { id: optionHost; width: 400 }
    }
    component SettingItem: Item { property string label; property real controlWidth }
    component QDrop: Item { property var choice; property var model; property bool useId; signal transformed(var choiced) }
    component QSlider: Item {
        property real from; property real to; property real stepSize; property bool leftText
        property string valueText; property real value; signal moved()
    }
    component QSwitch: Item { property bool switchTrue; signal toggled() }
    component QButton: Item { property real radius; property string text; property bool shadowEnabled }
%1
})").arg(dialog);
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/player-options-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({
        {"presentation", QVariant::fromValue(bridge.get())}, {"legacyPlayer", QVariant::fromValue<QObject *>(&legacy)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *preset = root->findChild<QObject *>("playbackRatePreset");
    auto *custom = root->findChild<QObject *>("playbackRateCustom"); QVERIFY(preset); QVERIFY(custom);
    auto *autoPlay = root->findChild<QObject *>("legacyAutoPlayOption"); QVERIFY(autoPlay);
    auto *gap = root->findChild<QObject *>("legacyGapCompensationOption"); QVERIFY(gap);
    auto *quality = root->findChild<QObject *>("legacyQualityOption"); QVERIFY(quality);
    auto *defaultOutput = root->findChild<QObject *>("legacyDefaultOutputOption"); QVERIFY(defaultOutput);
    auto *customOutput = root->findChild<QObject *>("legacyCustomOutputOption"); QVERIFY(customOutput);
    for (auto *item : {autoPlay, gap, quality, defaultOutput, customOutput}) QVERIFY(!item->property("enabled").toBool());
    QCOMPARE(legacy.autoPlayReads, 0); QVERIFY(defaultOutput->property("switchTrue").toBool());
    QVERIFY(QMetaObject::invokeMethod(autoPlay, "toggled")); QVERIFY(QMetaObject::invokeMethod(gap, "toggled"));
    QVERIFY(QMetaObject::invokeMethod(quality, "transformed", Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(defaultOutput, "toggled"));
    QVERIFY(QMetaObject::invokeMethod(customOutput, "transformed", Q_ARG(QVariant, 0)));
    QCOMPARE(legacy.autoPlayWrites, 0); QCOMPARE(legacy.autoPlayReads, 0); QVERIFY(!gap->property("switchTrue").toBool());
    const auto originalSettings = root->property("optionsFixture").toMap();
    QCOMPARE(originalSettings.value("settings").toMap().value("soundQuality").toInt(), 2);
    QCOMPARE(originalSettings.value("settings").toMap().value("audioDevice").toInt(), 1);
    QVERIFY(!originalSettings.value("settings").toMap().value("useDefaultDevice").toBool());
    QVERIFY(preset->property("enabled").toBool()); // Core rate is a global setting even when stopped.
    QVERIFY(QMetaObject::invokeMethod(preset, "transformed", Q_ARG(QVariant, 3)));
    QCOMPARE(controls.playbackRate, 1.25); QCOMPARE(controls.rateCalls, 1); QCOMPARE(legacy.playbackRate, 1.0);
    for (const QVariant &index : QVariantList{-1, 7, 1.5, QString("3")})
        QVERIFY(QMetaObject::invokeMethod(preset, "transformed", Q_ARG(QVariant, index)));
    QCOMPARE(controls.rateCalls, 1); QCOMPARE(root->property("playerRateIndex").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(preset, "transformed", Q_ARG(QVariant, 6)));
    QVERIFY(custom->property("enabled").toBool()); QVERIFY(custom->setProperty("value", 2.7));
    QVERIFY(QMetaObject::invokeMethod(custom, "moved")); QCOMPARE(controls.playbackRate, 2.7);
    const auto rateCalls = controls.rateCalls;
    for (double value : {-1.0, 0.0, 4.1, std::numeric_limits<double>::quiet_NaN()}) {
        QVERIFY(custom->setProperty("value", value)); QVERIFY(QMetaObject::invokeMethod(custom, "moved"));
    }
    QCOMPARE(controls.rateCalls, rateCalls);
    QVERIFY(bridge->setProperty("controls", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(!preset->property("enabled").toBool()); QVERIFY(!custom->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(preset, "transformed", Q_ARG(QVariant, 0)));
    QCOMPARE(root->property("playerRateIndex").toInt(), 6); QCOMPARE(legacy.playbackRate, 1.0);
    QVERIFY(bridge->setProperty("sourceMode", false));
    QVERIFY(autoPlay->property("enabled").toBool()); QVERIFY(quality->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(autoPlay, "toggled")); QCOMPARE(legacy.autoPlayWrites, 1); QVERIFY(!legacy.autoPlayValue);
    QVERIFY(QMetaObject::invokeMethod(preset, "transformed", Q_ARG(QVariant, 5))); QCOMPARE(legacy.playbackRate, 2.0);
    QCOMPARE(controls.rateCalls, rateCalls);
    QVERIFY(QMetaObject::invokeMethod(quality, "transformed", Q_ARG(QVariant, 1)));
    QCOMPARE(root->property("optionsFixture").toMap().value("settings").toMap().value("soundQuality").toInt(), 1);
}

void OriginalUiPlaybackQmlTest::sourceShutdownDoesNotMixLegacyPersistence()
{
    QQmlEngine engine; LegacyShutdownDouble legacy;
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto sync = capturedQmlBlock(source, source.indexOf("function syncSecureCurrent()"));
    auto closing = capturedQmlBlock(source, source.indexOf("function toClosing()"));
    QVERIFY(!sync.isEmpty()); QVERIFY(!closing.isEmpty()); QVERIFY(!sync.contains("currentItem"));
    closing.replace("Options.", "optionsFixture.");
    const auto fixture = QStringLiteral(R"(import QtQml
QtObject { id: window
    required property var legacy
    property var mainMedia: legacy; property var playListModel: legacy
    property bool securePlaybackCurrent: false; property bool sourceLyricsMode: false
    property string musicTitle: "Legacy Title"; property string musicArtist: "Legacy Artist"
    property bool minimized: false; property int closes: 0
    function showMinimized() { minimized = true; } function close() { ++closes; }
    property QtObject desktopSpot: QtObject { property bool active: true }
    property QtObject desktopLyricsLoader: QtObject { property bool active: true }
    property QtObject desktopPlayerLoader: QtObject { property bool active: true }
    property QtObject optionsFixture: QtObject {
        property var settings: ({closeToManage: false})
        property var lastSongs: ({name: "Previous", artist: "Previous Artist", hash: "previous-id", source: 2, cover: "old.png"})
    }
%1
%2
})").arg(sync, closing);
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/shutdown-boundary-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"legacy", QVariant::fromValue<QObject *>(&legacy)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *options = root->property("optionsFixture").value<QObject *>(); QVERIFY(options);
    const auto previous = options->property("lastSongs").toMap();
    QVERIFY(QMetaObject::invokeMethod(root.get(), "syncSecureCurrent")); QCOMPARE(legacy.stops, 0);
    QVERIFY(root->setProperty("securePlaybackCurrent", true));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "syncSecureCurrent"));
    QCOMPARE(legacy.stops, 1); QVERIFY(root->property("sourceLyricsMode").toBool());
    QCOMPARE(root->property("musicTitle").toString(), QString("Legacy Title"));
    QCOMPARE(root->property("musicArtist").toString(), QString("Legacy Artist"));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "toClosing")); QCOMPARE(legacy.reads, 0);
    QCOMPARE(options->property("lastSongs").toMap(), previous);
    for (auto name : {"desktopSpot", "desktopLyricsLoader", "desktopPlayerLoader"})
        QVERIFY(!root->property(name).value<QObject *>()->property("active").toBool());
    QCOMPARE(root->property("closes").toInt(), 1);
    QVERIFY(root->setProperty("securePlaybackCurrent", false)); // Source is sticky after stop.
    QVERIFY(QMetaObject::invokeMethod(root.get(), "toClosing")); QCOMPARE(legacy.reads, 0);
    QCOMPARE(options->property("lastSongs").toMap(), previous);
    QVERIFY(root->setProperty("sourceLyricsMode", false));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "toClosing")); QVERIFY(legacy.reads > 0);
    const auto saved = options->property("lastSongs").toMap();
    QCOMPARE(saved.value("name").toString(), QString("Legacy Title"));
    QCOMPARE(saved.value("artist").toString(), QString("Legacy Artist"));
    QCOMPARE(saved.value("hash").toString(), QString("legacy-id")); QCOMPARE(saved.value("source").toInt(), 1);
    QCOMPARE(saved.value("cover").toString(), QString("file:///legacy-cover.png"));
    legacy.rows = 0; legacy.reads = 0;
    QVERIFY(QMetaObject::invokeMethod(root.get(), "toClosing")); QCOMPARE(options->property("lastSongs").toMap(), saved);
    legacy.reads = 0;
    QVERIFY(options->setProperty("settings", QVariantMap{{"closeToManage", true}}));
    const auto closes = root->property("closes").toInt();
    QVERIFY(QMetaObject::invokeMethod(root.get(), "toClosing"));
    QVERIFY(root->property("minimized").toBool()); QCOMPARE(root->property("closes").toInt(), closes);
    QCOMPARE(legacy.reads, 0); QCOMPARE(options->property("lastSongs").toMap(), saved);
}

void OriginalUiPlaybackQmlTest::coverDialogUsesSafeCurrentLabelsAndFileNames()
{
    QQmlEngine engine;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"sourceMode", true}, {"sourceActive", true}, {"sourceItem", QVariantMap{{"title", "Current"}}},
        {"legacyDetails", QVariantMap{{"title", "Legacy"}}}})); QVERIFY(bridge);
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto mouse = capturedQmlBlock(source, source.lastIndexOf("MouseArea {", source.indexOf("id: coverWatchMouse")));
    const auto helper = capturedQmlBlock(source, source.indexOf("function safeFileName(title)"));
    const auto dialog = capturedQmlBlock(source, source.indexOf("function dialog(_source,_title)"));
    QVERIFY(!mouse.isEmpty()); QVERIFY(!helper.isEmpty()); QVERIFY(!dialog.isEmpty());
    const auto fixture = QStringLiteral(R"(import QtQuick
Item { id: musicpic; width: 100; height: 100
    required property var presentation
    property QtObject window: QtObject { property var lyricsAdapter: musicpic.presentation
        property string currentCover: "file:///fixture/host-cover.png" }
    property QtObject picWatch: QtObject { id: picWatch
        property string source; property string fileName; property int opens: 0
        property double coverRevision: 0
        function open() { ++opens; }
%1
%2
    }
    function clickCover() { coverWatchMouse.clicked(null); }
%3
})").arg(helper, dialog, mouse);
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/cover-label-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *popup = root->property("picWatch").value<QObject *>(); QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickCover")); QCOMPARE(popup->property("fileName").toString(), QString("Current.png"));
    QCOMPARE(popup->property("source").toString(), QString("file:///fixture/host-cover.png"));
    const QList<QPair<QString, QString>> cases{{"", "Picture.png"}, {"..", "Picture.png"}, {"../outside", ".._outside.png"},
        {"A/B\\C:D?", "A_B_C_D_.png"}, {"CON", "_CON.png"}, {"NUL.txt", "_NUL.txt.png"}, {"LPT1", "_LPT1.png"},
        {"  标题.  ", "标题.png"}, {QString("A") + QChar(0) + "B", "A_B.png"}};
    for (const auto &entry : cases) {
        QVariant result; QVERIFY(QMetaObject::invokeMethod(popup, "safeFileName", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, entry.first)));
        QCOMPARE(result.toString(), entry.second); QVERIFY(!result.toString().contains('/')); QVERIFY(!result.toString().contains('\\'));
    }
    QVariant result;
    const QString emoji = QString::fromUcs4(U"🎵");
    QVERIFY(QMetaObject::invokeMethod(popup, "safeFileName", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QString(63, QChar(u'中')) + emoji)));
    QCOMPARE(result.toString(), QString(63, QChar(u'中')) + ".png"); QVERIFY(result.toString().toUtf8().size() < 255);
    QVERIFY(bridge->setProperty("sourceActive", false));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickCover")); QCOMPARE(popup->property("fileName").toString(), QString("Picture.png"));
    QVERIFY(bridge->setProperty("sourceMode", false));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickCover")); QCOMPARE(popup->property("fileName").toString(), QString("Legacy.png"));
}

void OriginalUiPlaybackQmlTest::coverSaveKeepsTheCapturedDestination()
{
    QQmlEngine engine;
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.indexOf("id: picWatch"); QVERIFY(start >= 0);
    auto save = capturedQmlBlock(source, source.indexOf("onCancel: {", start)); QVERIFY(!save.isEmpty());
    save.replace("onCancel:", "function saveCover()");
    save.replace("StandardPaths.writableLocation(StandardPaths.PicturesLocation)", "\"/fixture/pictures\"");
    save.replace("Image.Ready", "1");
    save.replace("Image.Loading", "2");
    const auto fixture = QStringLiteral(R"(import QtQml
QtObject { id: picWatch; property string fileName: "First.png"; property string savedPath: ""
    property double coverRevision: 0; property bool savePending: false
    property QtObject imageWatch: QtObject { property int status: 1; property var pending: null
        function grabToImage(callback, size) { pending = callback; return true; }
        function complete() { var callback = pending; pending = null;
            callback({saveToFile: function(path) { picWatch.savedPath = path; return true; }}); }
    }
    property QtObject mainWarn: QtObject { function tiped(message, kind) {} }
%1
})").arg(save);
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/cover-save-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "saveCover"));
    QVERIFY(root->setProperty("fileName", "Second.png"));
    auto *image = root->property("imageWatch").value<QObject *>(); QVERIFY(image);
    QVERIFY(QMetaObject::invokeMethod(image, "complete"));
    QCOMPARE(root->property("savedPath").toString(), QString("/fixture/pictures/First.png"));
}

void OriginalUiPlaybackQmlTest::coverSaveRejectsFailuresAndChangedImages()
{
    QQmlEngine engine;
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.indexOf("id: picWatch"); QVERIFY(start >= 0);
    auto save = capturedQmlBlock(source, source.indexOf("onCancel: {", start)); QVERIFY(!save.isEmpty());
    save.replace("onCancel:", "function saveCover()");
    save.replace("StandardPaths.writableLocation(StandardPaths.PicturesLocation)", "picWatch.directory");
    save.replace("Image.Ready", "1"); save.replace("Image.Loading", "2");
    const auto sourceChanged = capturedQmlBlock(source, source.indexOf("onSourceChanged:", start));
    const auto dialog = capturedQmlBlock(source, source.indexOf("function dialog(_source,_title)", start));
    const auto helper = capturedQmlBlock(source, source.indexOf("function safeFileName(title)", start));
    QVERIFY(!sourceChanged.isEmpty()); QVERIFY(!dialog.isEmpty()); QVERIFY(!helper.isEmpty());
    const auto fixture = QStringLiteral(R"(import QtQml
QtObject { id: picWatch; property string fileName: "First.png"; property string source: "first"
    property double coverRevision: 0; property bool savePending: false
    property string directory: "/fixture/pictures"; property int writes: 0
    %1
    function open() {}
    property QtObject imageWatch: QtObject { property int status: 1; property var pending: null
        property int grabs: 0; property bool accepted: true; property int resultMode: 0
        function grabToImage(callback, size) { ++grabs; if (accepted) pending = callback; return accepted; }
        function complete() { var callback = pending; pending = null;
            if (resultMode === 2) { callback(null); return; }
            callback({saveToFile: function(path) {
                ++picWatch.writes;
                if (imageWatch.resultMode === 3) throw new Error("private path or diagnostic");
                return imageWatch.resultMode === 0;
            }});
        }
    }
    property QtObject mainWarn: QtObject { property string message; property int kind: -1; property int calls: 0
        function tiped(text, type) { message = text; kind = type; ++calls; } }
%2
%3
%4
})").arg(sourceChanged, helper, dialog, save);
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/cover-save-failures.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
    auto *image = root->property("imageWatch").value<QObject *>(); QVERIFY(image);
    auto *warn = root->property("mainWarn").value<QObject *>(); QVERIFY(warn);
    const auto saveCover = [&] { return QMetaObject::invokeMethod(root.get(), "saveCover"); };
    const auto complete = [&] { return QMetaObject::invokeMethod(image, "complete"); };
    QVERIFY(saveCover()); QVERIFY(root->property("savePending").toBool());
    QVERIFY(saveCover()); QCOMPARE(image->property("grabs").toInt(), 1); QCOMPARE(warn->property("calls").toInt(), 0);
    QVERIFY(complete()); QVERIFY(!root->property("savePending").toBool());
    QCOMPARE(warn->property("message").toString(), QString("已保存至系统图片文件夹")); QCOMPARE(warn->property("kind").toInt(), 1);
    for (int mode : {1, 2, 3}) {
        QVERIFY(image->setProperty("resultMode", mode)); QVERIFY(saveCover()); QVERIFY(complete());
        QVERIFY(!root->property("savePending").toBool()); QCOMPARE(warn->property("kind").toInt(), 2);
        QCOMPARE(warn->property("message").toString(), QString("图片保存失败，请检查文件夹权限或磁盘空间"));
    }
    const auto writes = root->property("writes").toInt();
    QVERIFY(image->setProperty("resultMode", 0)); QVERIFY(saveCover());
    QVERIFY(root->setProperty("source", "second")); QVERIFY(root->setProperty("source", "first"));
    QVERIFY(complete()); QCOMPARE(root->property("writes").toInt(), writes);
    QCOMPARE(warn->property("message").toString(), QString("图片已变化，请重新保存"));
    QVERIFY(saveCover()); // Reopening even the same source invalidates the previous view's grab.
    QVERIFY(QMetaObject::invokeMethod(root.get(), "dialog", Q_ARG(QVariant, QStringLiteral("first")), Q_ARG(QVariant, QStringLiteral("Reopened"))));
    QVERIFY(complete()); QCOMPARE(root->property("writes").toInt(), writes);
    QVERIFY(saveCover()); QVERIFY(image->setProperty("status", 3)); QVERIFY(complete());
    QCOMPARE(root->property("writes").toInt(), writes); QVERIFY(!root->property("savePending").toBool());
    const auto grabs = image->property("grabs").toInt();
    for (int status : {0, 2, 3}) {
        QVERIFY(image->setProperty("status", status)); QVERIFY(saveCover());
        QCOMPARE(image->property("grabs").toInt(), grabs); QCOMPARE(root->property("writes").toInt(), writes);
        QCOMPARE(warn->property("message").toString(), status == 2 ? QString("图片正在快速加载") : QString("图片不可用，请重新加载"));
    }
    QVERIFY(image->setProperty("status", 1)); QVERIFY(root->setProperty("directory", "")); QVERIFY(saveCover());
    QCOMPARE(image->property("grabs").toInt(), grabs); QCOMPARE(warn->property("message").toString(), QString("系统图片文件夹不可用"));
    QVERIFY(root->setProperty("directory", "/fixture/pictures")); QVERIFY(image->setProperty("accepted", false));
    QVERIFY(saveCover()); QVERIFY(!root->property("savePending").toBool()); QCOMPARE(root->property("writes").toInt(), writes);
    QCOMPARE(warn->property("message").toString(), QString("图片捕获失败，请重新保存"));
    QVERIFY(image->setProperty("accepted", true)); QVERIFY(saveCover()); QVERIFY(complete());
    QCOMPARE(root->property("writes").toInt(), writes + 1); QCOMPARE(warn->property("kind").toInt(), 1);
}

void OriginalUiPlaybackQmlTest::actualSettingsNavigationPreservesPluginAndInstanceSelection()
{
    QQmlEngine engine;
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto block = [&source](qsizetype start) {
        if (start < 0) return QString();
        int depth = 0;
        for (auto end = start; end < source.size(); ++end) {
            if (source[end] == '{') ++depth;
            else if (source[end] == '}' && --depth == 0) return source.mid(start, end + 1 - start);
        }
        return QString();
    };
    const auto open = block(source.indexOf("function openPluginSettings(packageId, instanceId)")); QVERIFY(!open.isEmpty());
    const auto loader = source.indexOf("id: settingsView"); QVERIFY(loader >= 0);
    auto loaded = block(source.indexOf("onLoaded: {", loader)); QVERIFY(!loaded.isEmpty());
    loaded.replace("onLoaded:", "function fireLoaded()");
    const auto fixture = QStringLiteral(R"(import QtQml
QtObject { id: window
    property QtObject settingsView: QtObject {
        property bool active: false; property bool visible: false; property var item: null
        property string pendingPluginPackageId: ""; property string pendingPluginInstanceId: ""
%1
    }
    property QtObject settingAnime: QtObject { property bool running: false }
    property QtObject page: QtObject {
        property var selections: []
        function openPluginSettings(packageId, instanceId) {
            selections = selections.concat([{packageId: packageId, instanceId: instanceId}]);
        }
    }
%2
})").arg(loaded, open);
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/settings-navigation-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
    auto *settings = root->property("settingsView").value<QObject *>(), *page = root->property("page").value<QObject *>();
    QVERIFY(settings); QVERIFY(page);
    const auto select = [&root](const QString &package, const QString &instance) {
        return QMetaObject::invokeMethod(root.get(), "openPluginSettings", Q_ARG(QVariant, package), Q_ARG(QVariant, instance));
    };
    QVERIFY(select("fixture.alpha", "fixture.alpha/account-a"));
    QVERIFY(settings->property("active").toBool()); QVERIFY(!settings->property("visible").toBool());
    QCOMPARE(settings->property("pendingPluginPackageId").toString(), QString("fixture.alpha"));
    QCOMPARE(settings->property("pendingPluginInstanceId").toString(), QString("fixture.alpha/account-a"));
    QVERIFY(select("fixture.beta", "fixture.beta/account-b")); // Latest request wins before load.
    QVERIFY(settings->setProperty("item", QVariant::fromValue(page)));
    QVERIFY(QMetaObject::invokeMethod(settings, "fireLoaded"));
    QCOMPARE(page->property("selections").toList(), QVariantList({QVariantMap{{"packageId", "fixture.beta"}, {"instanceId", "fixture.beta/account-b"}}}));
    QVERIFY(settings->property("visible").toBool());
    QVERIFY(settings->property("pendingPluginPackageId").toString().isEmpty());
    QVERIFY(settings->property("pendingPluginInstanceId").toString().isEmpty());
    QVERIFY(select("fixture.alpha", "fixture.alpha/account-a"));
    QCOMPARE(page->property("selections").toList().last().toMap(), QVariantMap({{"packageId", "fixture.alpha"}, {"instanceId", "fixture.alpha/account-a"}}));
    QVERIFY(select("fixture.alpha", ""));
    QCOMPARE(page->property("selections").toList().last().toMap(), QVariantMap({{"packageId", "fixture.alpha"}, {"instanceId", ""}}));
    QVERIFY(QMetaObject::invokeMethod(settings, "fireLoaded"));
    QCOMPARE(page->property("selections").toList().size(), 3); // Cleared request is not replayed.
}

void OriginalUiPlaybackQmlTest::remainingPlaybackCaptionsUseSafePresentation_data()
{
    QTest::addColumn<bool>("maximized");
    QTest::newRow("maximized-player") << true;
    QTest::newRow("desktop-lyrics") << false;
}

void OriginalUiPlaybackQmlTest::remainingPlaybackCaptionsUseSafePresentation()
{
    QFETCH(bool, maximized);
    QQmlEngine engine;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"sourceMode", true}, {"sourceActive", true}, {"sourceItem", QVariantMap{{"title", "<b>Current</b>"},
            {"artists", QStringList{"A", "B"}}}},
        {"legacyDetails", QVariantMap{{"title", "Old Title"}, {"artist", "Old Artist"}}}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/") + (maximized ? "layout/PlayerMaxCenter.qml" : "components/DesktopLyrics.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly)); const auto source = QString::fromUtf8(file.readAll());
    QVERIFY(!source.contains("window.musicTitle")); QVERIFY(!source.contains("window.musicArtist"));
    const auto extract = [&source](const QString &id) {
        const auto declaration = source.indexOf("id: " + id);
        if (declaration < 0) return QString();
        const auto start = source.lastIndexOf("Text {", declaration);
        if (start < 0) return QString();
        int depth = 0;
        for (auto end = start; end < source.size(); ++end) {
            if (source[end] == '{') ++depth;
            else if (source[end] == '}' && --depth == 0) return source.mid(start, end + 1 - start);
        }
        return QString();
    };
    QString fragment;
    for (const auto &id : maximized ? QStringList{"titleMax", "artistMax", "lyricModeText"} : QStringList{"playbackCaption"}) {
        const auto text = extract(id); QVERIFY(!text.isEmpty()); fragment += text;
    }
    auto fixture = QStringLiteral(R"(import QtQuick
Item { id: musicControlMax; width: 800; height: 600
    required property var presentation
    readonly property real standHeight: 48
    property QtObject window: QtObject { property var lyricsAdapter: musicControlMax.presentation }
    property var mainLayout: ({height: 600, width: 800, piclong: 300})
    property var controlMaxLoader: ({infoX: 0, lyricsType: 2, basicCd: false})
    property var desktopLyricsWindow: ({width: 800})
    property var styleFixture: ({settings: {text: 16}})
    // Effect stand-in only: metadata bindings and Text nodes are production code.
    component DropShadow: Item { property real horizontalOffset; property real verticalOffset; property real radius;
        property int samples; property bool fast; property color color; property Item source }
%1
})").arg(fragment);
    fixture.replace("Style.", "musicControlMax.styleFixture.");
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/remaining-captions-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *caption = root->findChild<QObject *>(maximized ? "maxPlayerCaption" : "desktopLyricsCaption"); QVERIFY(caption);
    const auto separator = maximized ? QString("    --") : QString(" - ");
    QCOMPARE(caption->property("text").toString(), QString("<b>Current</b>") + separator + "A, B");
    QCOMPARE(caption->property("textFormat").toInt(), 0);
    if (maximized) {
        auto *title = root->findChild<QObject *>("maxPlayerTitle"), *artist = root->findChild<QObject *>("maxPlayerArtist");
        QVERIFY(title); QVERIFY(artist); QCOMPARE(title->property("text").toString(), QString("<b>Current</b>"));
        QCOMPARE(artist->property("text").toString(), QString("A, B"));
        QCOMPARE(title->property("textFormat").toInt(), 0); QCOMPARE(artist->property("textFormat").toInt(), 0);
    }
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{{"title", "Title only"}}));
    QCOMPARE(caption->property("text").toString(), QString("Title only"));
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{{"artists", QStringList{"Artist only"}}}));
    QCOMPARE(caption->property("text").toString(), QString("Artist only"));
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{})); QVERIFY(caption->property("text").toString().isEmpty());
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{{"title", "Last track"}, {"artists", QStringList{"Last artist"}}}));
    QVERIFY(bridge->setProperty("sourceActive", false));
    for (auto *text : root->findChildren<QObject *>())
        if (text->objectName().startsWith("maxPlayer") || text == caption) QVERIFY(text->property("text").toString().isEmpty());
    QVERIFY(bridge->setProperty("legacyDetails", QVariantMap{{"title", "Late old title"}, {"artist", "Late old artist"}}));
    QVERIFY(caption->property("text").toString().isEmpty());
    QVERIFY(bridge->setProperty("sourceMode", false));
    QCOMPARE(caption->property("text").toString(), QString("Late old title") + separator + "Late old artist");
}

void OriginalUiPlaybackQmlTest::actualMenuLabelsTreatPluginTextAsPlainText()
{
    QQmlEngine engine;
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/components/QMenu.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    auto source = QString::fromUtf8(file.readAll());
    source.replace("property Item blurSource: mainLayout", "property Item blurSource: null");
    source.replace("Style.", "dialog.styleFixture.");
    const auto start = source.indexOf("Menu {"); QVERIFY(start >= 0);
    source.insert(start + 6, QStringLiteral(R"(
    property var styleFixture: ({themes: {hoverColor: "#cccccc", fontColor: "#222222"}, settings: {labelRadius: 8, textmain: 14}})
    component QBlurCard: Rectangle { property bool shadowEffect; property Item blurSource;
        property bool masked; property var rectXy; property real borderRadius }
)"));
    QQmlComponent component(&engine); component.setData(source.toUtf8(), QUrl("qrc:/menu-label-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QString label("<b>Literal plugin artist</b>");
    std::unique_ptr<QObject> menu(component.createWithInitialProperties({{"model", QStringList{label}}}));
    QVERIFY2(menu, qPrintable(component.errorString()));
    const auto labels = menu->findChildren<QObject *>("menuDisplayText"); QVERIFY(!labels.isEmpty());
    for (auto *text : labels) {
        QCOMPARE(text->property("text").toString(), label);
        QCOMPARE(text->property("textFormat").toInt(), 0);
    }
}

void OriginalUiPlaybackQmlTest::actualPlayerMetadataMenusUseSafeDisplaySnapshots()
{
    QQmlEngine engine;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"sourceMode", true}, {"sourceActive", true}, {"sourceItem", QVariantMap{{"title", "<b>Source Title</b>"},
            {"artists", QStringList{"Alpha / Beta", "Gamma"}}}},
        {"legacyDetails", QVariantMap{{"title", "Old Title"}, {"artist", "Old Artist"}}}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto block = [&source](qsizetype start) {
        if (start < 0) return QString();
        int depth = 0;
        for (auto end = start; end < source.size(); ++end) {
            if (source[end] == '{') ++depth;
            else if (source[end] == '}' && --depth == 0) return source.mid(start, end + 1 - start);
        }
        return QString();
    };
    const auto title = block(source.lastIndexOf("Text {", source.indexOf("id: titleDisplay")));
    const auto artist = block(source.lastIndexOf("Text {", source.indexOf("id: artistDisplay")));
    // Explicit Legacy favorites still use compatibility fields; only these
    // display/search fragments must be free of them.
    QVERIFY(!title.contains("window.musicTitle")); QVERIFY(!artist.contains("window.musicArtist"));
    const auto parse = block(source.indexOf("function parseArtists(raw)"));
    const auto search = block(source.indexOf("function doSearchSongsMessage(name)"));
    const auto headerStart = source.indexOf("readonly property string currentTitle:");
    const auto headerEnd = source.indexOf("readonly property string mediaTime:", headerStart);
    QVERIFY(!title.isEmpty()); QVERIFY(!artist.isEmpty()); QVERIFY(!parse.isEmpty()); QVERIFY(!search.isEmpty());
    QVERIFY(headerStart >= 0 && headerEnd > headerStart);
    auto fixture = QStringLiteral(R"(import QtQuick
Item { id: musicControlMin; width: 320; height: 90
    required property var presentation
    property QtObject window: QtObject {
        property var lyricsAdapter: musicControlMin.presentation; property int exitIndex: 0
        property QtObject musicAdapter: QtObject { property var queries: []; property int page: -1
            function search(query, offset) { queries = queries.concat([query]); page = offset; } }
    }
    property QtObject mainSearchInput: QtObject { property string text: "" }
    property QtObject mainContent: QtObject { property int index: -1; function contentIndexed(value) { index = value; } }
    property var styleFixture: ({themes: {themeColor: "#00ff00", textColor: "#222222"}})
    component QMenu: Item { property list<string> model: []; property bool masked; property var blurSource
        property int popupCalls: 0; property int closeCalls: 0; signal clicked(int index)
        function popup() { ++popupCalls; } function close() { ++closeCalls; } }
    function clickTitle() { titleDisplayMouse.clicked(null); }
    function clickArtist() { artistDisplayMouse.clicked(null); }
%1
%2
%3
%4
%5
})").arg(source.mid(headerStart, headerEnd - headerStart), parse, search, title, artist);
    fixture.replace("Style.", "musicControlMin.styleFixture.");
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/metadata-menu-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *titleText = root->findChild<QObject *>("originalPlayerTitle"); QVERIFY(titleText);
    auto *artistText = root->findChild<QObject *>("originalPlayerArtist"); QVERIFY(artistText);
    auto *titleMenu = root->findChild<QObject *>("originalPlayerTitleMenu"); QVERIFY(titleMenu);
    auto *artistMenu = root->findChild<QObject *>("originalPlayerArtistMenu"); QVERIFY(artistMenu);
    auto *window = root->property("window").value<QObject *>(); QVERIFY(window);
    auto *adapter = window->property("musicAdapter").value<QObject *>(); QVERIFY(adapter);
    QCOMPARE(titleText->property("text").toString(), QString("<b>Source Title</b>"));
    QCOMPARE(artistText->property("text").toString(), QString("Alpha / Beta, Gamma"));
    QCOMPARE(titleText->property("textFormat").toInt(), 0); QCOMPARE(artistText->property("textFormat").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickTitle"));
    QVERIFY(QMetaObject::invokeMethod(titleMenu, "clicked", Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickArtist"));
    QCOMPARE(artistMenu->property("model").toStringList(), QStringList({"搜索", "Alpha", "Beta", "Gamma"}));
    QVERIFY(QMetaObject::invokeMethod(artistMenu, "clicked", Q_ARG(int, 2)));
    QCOMPARE(adapter->property("queries").toList(), QVariantList({"<b>Source Title</b>", "Beta"}));
    QCOMPARE(adapter->property("page").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(artistMenu, "clicked", Q_ARG(int, -1)));
    QVERIFY(QMetaObject::invokeMethod(artistMenu, "clicked", Q_ARG(int, 10)));
    QCOMPARE(adapter->property("queries").toList().size(), 2);
    const auto titleCloses = titleMenu->property("closeCalls").toInt(), artistCloses = artistMenu->property("closeCalls").toInt();
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{{"title", "Next Title"}, {"artists", QStringList{"Next Artist"}}}));
    QVERIFY(titleMenu->property("closeCalls").toInt() > titleCloses); QVERIFY(artistMenu->property("closeCalls").toInt() > artistCloses);
    QVERIFY(QMetaObject::invokeMethod(titleMenu, "clicked", Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(artistMenu, "clicked", Q_ARG(int, 1)));
    QCOMPARE(adapter->property("queries").toList().size(), 2);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickTitle"));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickArtist"));
    QVERIFY(bridge->setProperty("sourceActive", false));
    QVERIFY(titleText->property("text").toString().isEmpty()); QVERIFY(artistText->property("text").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(titleMenu, "clicked", Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(artistMenu, "clicked", Q_ARG(int, 1)));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickTitle"));
    QCOMPARE(adapter->property("queries").toList().size(), 2);
    QVERIFY(bridge->setProperty("sourceMode", false));
    QCOMPARE(titleText->property("text").toString(), QString("Old Title"));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "clickTitle"));
    QVERIFY(QMetaObject::invokeMethod(titleMenu, "clicked", Q_ARG(int, 0)));
    QCOMPARE(adapter->property("queries").toList().constLast().toString(), QString("Old Title"));
}

void OriginalUiPlaybackQmlTest::originalProgressRangesFollowPresentationChanges_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("desktop");
    QTest::newRow("desktop-player") << QString("components/DesktopPlayerWindow.qml") << true;
    QTest::newRow("original-bottom-bar") << QString("layout/PlayerControl.qml") << false;
}

void OriginalUiPlaybackQmlTest::originalProgressRangesFollowPresentationChanges()
{
    QFETCH(QString, path); QFETCH(bool, desktop);
    QQmlEngine engine; PlaybackControllerDouble controls; LegacyPlayerDouble legacy;
    controls.position = 4200; controls.duration = 5000;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"controls", QVariant::fromValue<QObject *>(&controls)}, {"legacyPlayer", QVariant::fromValue<QObject *>(&legacy)},
        {"sourceMode", true}, {"sourceActive", false}, {"sourceItem", QVariantMap{{"title", "<b>Source</b>"},
            {"artists", QStringList{"Artist A", "Artist B"}}}}, {"legacyDetails", QVariantMap{{"title", "Old"}, {"artist", "Old Artist"}}},
        {"legacyPosition", 8000}, {"legacyDuration", 10000}, {"legacyActive", true}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/") + path); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto extract = [&source](const QString &type, const QString &id) {
        const auto declaration = source.indexOf("id: " + id);
        const auto start = source.lastIndexOf(type + " {", declaration);
        if (declaration < 0 || start < 0) return QString();
        int depth = 0;
        for (auto end = start; end < source.size(); ++end) {
            if (source[end] == '{') ++depth;
            else if (source[end] == '}' && --depth == 0) return source.mid(start, end + 1 - start);
        }
        return QString();
    };
    auto fragment = extract("Slider", desktop ? "seekSlider" : "progressSlider"); QVERIFY(!fragment.isEmpty());
    if (desktop) {
        QVERIFY(!source.contains("window.musicTitle")); QVERIFY(!source.contains("window.musicArtist"));
        const auto title = extract("Text", "playerTitle"), artist = extract("Text", "playerArtist");
        QVERIFY(!title.isEmpty()); QVERIFY(!artist.isEmpty()); fragment += title + artist;
    }
    auto fixture = QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
Item { id: musicControlMin; width: 320; height: 90
    required property var presentation
    property QtObject window: QtObject { property var lyricsAdapter: musicControlMin.presentation }
    readonly property real currentPosition: presentation.position
    readonly property real currentDuration: presentation.duration
    readonly property string mediaTime: String(currentPosition)
    property QtObject playerCard: QtObject { property real width: 320 }
    property QtObject sliderControl: QtObject { property real width: 320 }
    property var styleFixture: ({themes: {primaryColor: "#222222", secondaryColor: "#444444", sideColor: "#333333",
        themeColor: "#00ff00", fontColor: "#ffffff", textColor: "#ffffff"}})
%1
})").arg(fragment);
    fixture.replace("Style.", "musicControlMin.styleFixture.");
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/original-progress-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *slider = root->findChild<QObject *>(desktop ? "desktopPlayerSeekSlider" : "originalPlayerSeekSlider"); QVERIFY(slider);
    QObject *title = nullptr, *artist = nullptr;
    if (desktop) {
        title = root->findChild<QObject *>("desktopPlayerTitle"); artist = root->findChild<QObject *>("desktopPlayerArtist");
        QVERIFY(title); QVERIFY(artist); QVERIFY(title->property("text").toString().isEmpty());
        QVERIFY(artist->property("text").toString().isEmpty());
    }
    QCOMPARE(slider->property("to").toReal(), 1.0); QCOMPARE(slider->property("value").toReal(), 0.0);
    QVERIFY(bridge->setProperty("sourceActive", true));
    QCOMPARE(slider->property("to").toReal(), 5000.0); QCOMPARE(slider->property("value").toReal(), 4200.0);
    if (desktop) {
        QCOMPARE(title->property("text").toString(), QString("<b>Source</b>"));
        QCOMPARE(artist->property("text").toString(), QString("Artist A, Artist B"));
        QCOMPARE(title->property("textFormat").toInt(), 0); QCOMPARE(artist->property("textFormat").toInt(), 0);
    }
    controls.duration = 1000; emit controls.durationChanged();
    QCOMPARE(slider->property("value").toReal(), 1000.0);
    controls.duration = 5000; emit controls.durationChanged();
    QCOMPARE(slider->property("value").toReal(), 4200.0); // Position did not change.
    QVERIFY(slider->setProperty("pressed", true)); QVERIFY(slider->setProperty("value", 3000));
    controls.position = 2500; emit controls.positionChanged();
    QCOMPARE(slider->property("value").toReal(), 3000.0);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved")); QCOMPARE(controls.sought, 3000);
    QVERIFY(slider->setProperty("pressed", false)); QCOMPARE(slider->property("value").toReal(), 2500.0);
    QVERIFY(bridge->setProperty("sourceActive", false));
    QCOMPARE(slider->property("value").toReal(), 0.0); QVERIFY(!slider->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(slider, "moved")); QCOMPARE(controls.sought, 3000); QCOMPARE(legacy.position, 0);
    if (desktop) { QVERIFY(title->property("text").toString().isEmpty()); QVERIFY(artist->property("text").toString().isEmpty()); }
    QVERIFY(bridge->setProperty("sourceMode", false));
    QCOMPARE(slider->property("to").toReal(), 10000.0); QCOMPARE(slider->property("value").toReal(), 8000.0);
    if (desktop) { QCOMPARE(title->property("text").toString(), QString("Old")); QCOMPARE(artist->property("text").toString(), QString("Old Artist")); }
    QVERIFY(bridge->setProperty("sourceMode", true));
    QVERIFY(bridge->setProperty("sourceActive", true));
    QCOMPARE(slider->property("value").toReal(), 2500.0);
}

void OriginalUiPlaybackQmlTest::actualDesktopSpotUsesSafePlaybackPresentation()
{
    QQmlEngine engine;
    PlaybackControllerDouble controls; LegacyPlayerDouble legacy;
    controls.duration = 5000; controls.position = 350;
    legacy.playing = true; legacy.position = 777;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"controls", QVariant::fromValue<QObject *>(&controls)}, {"legacyPlayer", QVariant::fromValue<QObject *>(&legacy)},
        {"sourceMode", true}, {"sourceActive", true}, {"sourceItem", QVariantMap{{"title", "Source Track"}}},
        {"legacyDetails", QVariantMap{{"title", "Old Track"}}}, {"legacyDuration", 9999},
        {"legacyPosition", 777}, {"legacyActive", true}, {"legacyPlaying", true}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/components/DesktopSpot.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    auto source = QString::fromUtf8(file.readAll());
    QVERIFY(!source.contains("mainMedia")); QVERIFY(!source.contains("window.musicTitle"));
    // Execute the complete production component. Only private visual/theme
    // dependencies are stand-ins; handlers, Slider, Binding and animations are real.
    source.replace("Style.", "desktopSpot.styleFixture.");
    source.replace("iconFont.name", "\"sans-serif\"");
    const auto start = source.indexOf("Window {"); QVERIFY(start >= 0);
    source.insert(start + 8, QStringLiteral(R"(
    required property var presentation
    property var styleFixture: ({themes: {fontColor: "#ffffff", primaryColor: "#222222", secondaryColor: "#444444",
        secondaryBlurColor: "#666666", textColor: "#ffffff", themeColor: "#00ff00"}, settings: {texticon: 16}})
    property QtObject window: QtObject { property var lyricsAdapter: desktopSpot.presentation }
    property QtObject musicControlMin: QtObject { property int previous: 0; property int next: 0;
        function lastMedia() { ++previous } function enterMedia() { ++next } }
    component SButton: Item { signal clicked(); property string iconCharacter; property real radius;
        property color buttonColor; property color hoverColor; property color iconColor;
        property real iconSize; property bool shadowEnabled }
)"));
    QQmlComponent component(&engine); component.setData(source.toUtf8(), QUrl("qrc:/desktop-spot-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *title = root->findChild<QObject *>("desktopSpotTitle"); QVERIFY(title);
    auto *play = root->findChild<QObject *>("desktopSpotPlayButton"); QVERIFY(play);
    auto *slider = root->findChild<QObject *>("desktopSpotSeekSlider"); QVERIFY(slider);
    auto *previous = root->findChild<QObject *>("desktopSpotPreviousButton"); QVERIFY(previous);
    auto *next = root->findChild<QObject *>("desktopSpotNextButton"); QVERIFY(next);
    auto *navigation = root->property("musicControlMin").value<QObject *>(); QVERIFY(navigation);
    QCOMPARE(title->property("text").toString(), QString("Source Track"));
    QCOMPARE(title->property("textFormat").toInt(), 0); // PlainText, never plugin markup.
    QCOMPARE(play->property("iconCharacter").toString(), QString(QChar(0xf00e)));
    QVERIFY(play->property("enabled").toBool());
    QCOMPARE(slider->property("to").toReal(), 5000.0);
    QCOMPARE(slider->property("value").toReal(), 350.0);
    QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QCOMPARE(controls.playCalls, 1);
    controls.playing = true; emit controls.playingChanged();
    QCOMPARE(play->property("iconCharacter").toString(), QString(QChar(0xf02f)));
    QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QCOMPARE(controls.pauseCalls, 1);
    QVERIFY(QMetaObject::invokeMethod(previous, "clicked")); QCOMPARE(navigation->property("previous").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(next, "clicked")); QCOMPARE(navigation->property("next").toInt(), 1);
    QVERIFY(slider->setProperty("pressed", true));
    QVERIFY(slider->setProperty("value", 1500));
    controls.position = 900; emit controls.positionChanged();
    QCOMPARE(slider->property("value").toReal(), 1500.0);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved")); QCOMPARE(controls.sought, 1500);
    QVERIFY(slider->setProperty("pressed", false));
    QCOMPARE(slider->property("value").toReal(), 900.0);
    controls.seekable = false; emit controls.seekableChanged();
    QVERIFY(!slider->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(slider, "moved")); QCOMPARE(controls.sought, 1500);
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{{"title", "<b>Literal Source Title</b>"}}));
    QCOMPARE(title->property("text").toString(), QString("<b>Literal Source Title</b>"));
    QVERIFY(bridge->setProperty("sourceActive", false));
    QVERIFY(title->property("text").toString().isEmpty());
    QVERIFY(!play->property("enabled").toBool()); QVERIFY(!next->property("enabled").toBool());
    QCOMPARE(slider->property("to").toReal(), 1.0); QCOMPARE(slider->property("value").toReal(), 0.0);
    QVERIFY(QMetaObject::invokeMethod(play, "clicked"));
    QVERIFY(QMetaObject::invokeMethod(previous, "clicked")); QVERIFY(QMetaObject::invokeMethod(next, "clicked"));
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QCOMPARE(controls.playCalls, 1); QCOMPARE(controls.pauseCalls, 1); QCOMPARE(controls.sought, 1500);
    QCOMPARE(navigation->property("previous").toInt(), 1); QCOMPARE(navigation->property("next").toInt(), 1);
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.pauseCalls, 0); QCOMPARE(legacy.position, 777);
    QVERIFY(bridge->setProperty("sourceActive", true));
    QVERIFY(bridge->setProperty("controls", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.pauseCalls, 0);
    QVERIFY(bridge->setProperty("sourceMode", false));
    QCOMPARE(title->property("text").toString(), QString("Old Track"));
    QCOMPARE(slider->property("to").toReal(), 9999.0); QCOMPARE(slider->property("value").toReal(), 777.0);
    QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QCOMPARE(legacy.pauseCalls, 1);
    legacy.playing = false;
    QVERIFY(QMetaObject::invokeMethod(play, "clicked")); QCOMPARE(legacy.playCalls, 1);
    QVERIFY(slider->setProperty("pressed", true)); QVERIFY(slider->setProperty("value", 2000));
    QVERIFY(QMetaObject::invokeMethod(slider, "moved")); QCOMPARE(legacy.position, 2000);
}

void OriginalUiPlaybackQmlTest::actualSpectrumBindingControlsCoreFromOriginalDisplayPreference()
{
    QQmlEngine engine;
    QtPlaybackController first, second;
    QQmlComponent controlsComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackControlsAdapter.qml"));
    QVERIFY2(controlsComponent.isReady(), qPrintable(controlsComponent.errorString()));
    std::unique_ptr<QObject> firstControls(controlsComponent.createWithInitialProperties({
        {"controller", QVariant::fromValue<QObject *>(&first)}}));
    std::unique_ptr<QObject> secondControls(controlsComponent.createWithInitialProperties({
        {"controller", QVariant::fromValue<QObject *>(&second)}}));
    QVERIFY(firstControls); QVERIFY(secondControls);
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.indexOf("    Binding {"); QVERIFY(start >= 0);
    const auto end = source.indexOf("\n    }", start); QVERIFY(end > start);
    auto binding = source.mid(start, end + 6 - start);
    QVERIFY(binding.contains("property: \"spectrumEnabled\""));
    binding.replace("Style.settings.waveDisplay", "window.displayWave");
    QQmlComponent component(&engine);
    component.setData((QStringLiteral("import QtQml\nQtObject { id: window; required property var playbackAdapter; "
        "property bool sourceLyricsMode: false; property bool securePlaybackCurrent: false; property bool displayWave: true; "
        "property Binding spectrumBinding: ") + binding + "\n}").toUtf8(), QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({
        {"playbackAdapter", QVariant::fromValue(firstControls.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    QVERIFY(!first.spectrumEnabled());
    QVERIFY(root->setProperty("sourceLyricsMode", true));
    QVERIFY(!first.spectrumEnabled());
    QVERIFY(root->setProperty("securePlaybackCurrent", true));
    QVERIFY(first.spectrumEnabled());
    QVERIFY(root->setProperty("displayWave", false));
    QVERIFY(!first.spectrumEnabled());
    QVERIFY(root->setProperty("displayWave", true));
    QVERIFY(first.spectrumEnabled());
    QVERIFY(root->setProperty("playbackAdapter", QVariant::fromValue(secondControls.get())));
    QVERIFY(!first.spectrumEnabled()); QVERIFY(second.spectrumEnabled());
    QVERIFY(root->setProperty("playbackAdapter", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(!second.spectrumEnabled());
    QVERIFY(root->setProperty("playbackAdapter", QVariant::fromValue(secondControls.get())));
    QVERIFY(second.spectrumEnabled());
    QVERIFY(root->setProperty("securePlaybackCurrent", false));
    QVERIFY(!second.spectrumEnabled());
    QVERIFY(root->setProperty("securePlaybackCurrent", true));
    QVERIFY(second.spectrumEnabled());
    QVERIFY(root->setProperty("sourceLyricsMode", false));
    QVERIFY(!second.spectrumEnabled());
}

void OriginalUiPlaybackQmlTest::spectrumBridgeRejectsLegacyFramesInStickySourceMode()
{
    QQmlEngine engine;
    PlaybackControllerDouble controls;
    controls.playing = true;
    controls.wavePath = {QPointF(0, 80), QPointF(256, 25), QPointF(512, 80)};
    QQmlComponent component(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QVariantList legacy{QPointF(1, 2), QPointF(3, 4)};
    std::unique_ptr<QObject> bridge(component.createWithInitialProperties({
        {"sourceMode", true}, {"sourceActive", true}, {"legacyWavePath", legacy},
        {"controls", QVariant::fromValue<QObject *>(&controls)}})); QVERIFY(bridge);
    QVERIFY(bridge->property("wavePath").toList().isEmpty());
    QVERIFY(bridge->setProperty("waveEnabled", true));
    QCOMPARE(bridge->property("wavePath").toList(), controls.wavePath);
    controls.playing = false; emit controls.playingChanged();
    QVERIFY(bridge->property("wavePath").toList().isEmpty());
    controls.playing = true; emit controls.playingChanged();
    QCOMPARE(bridge->property("wavePath").toList(), controls.wavePath);
    QVERIFY(bridge->setProperty("waveEnabled", false));
    QVERIFY(bridge->property("wavePath").toList().isEmpty());
    QVERIFY(bridge->setProperty("waveEnabled", true));
    QVERIFY(bridge->setProperty("sourceActive", false)); QVERIFY(bridge->property("wavePath").toList().isEmpty());
    QVERIFY(bridge->setProperty("legacyWavePath", QVariantList{QPointF(5, 6)}));
    QVERIFY(bridge->property("wavePath").toList().isEmpty());
    QVERIFY(bridge->setProperty("sourceMode", false)); QCOMPARE(bridge->property("wavePath").toList(), QVariantList{QPointF(5, 6)});
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto main = QString::fromUtf8(file.readAll());
    QVERIFY(main.contains("mediaPlayer: window.sourceLyricsMode ? null : mainMedia"));
    QVERIFY(main.contains("enabled: !window.sourceLyricsMode && Style.settings.waveDisplay && mainMedia.playing"));
    QVERIFY(main.contains("value: window.sourceLyricsMode && window.securePlaybackCurrent && Style.settings.waveDisplay"));
    QVERIFY(main.contains("waveEnabled: Style.settings.waveDisplay"));
    QFile maxFile(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerMaxCenter.qml")); QVERIFY(maxFile.open(QIODevice::ReadOnly));
    const auto max = QString::fromUtf8(maxFile.readAll()); QVERIFY(!max.contains("getWave.wavePath"));
    QVERIFY(max.contains("path: window.lyricsAdapter.wavePath"));
}

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
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "play")); QCOMPARE(controls.playCalls, 2);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "pause")); QCOMPARE(controls.pauseCalls, 2);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 640))); QCOMPARE(controls.sought, 640);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, -1))); QCOMPARE(controls.sought, 640);
    controls.seekable = false; emit controls.seekableChanged();
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 200))); QCOMPARE(controls.sought, 640);
    QVERIFY(adapter->setProperty("sourceActive", false));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "play"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "pause"));
    QCOMPARE(controls.playCalls, 2); QCOMPARE(controls.pauseCalls, 2);
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.pauseCalls, 0); QCOMPARE(legacy.position, 0);
    QVERIFY(adapter->setProperty("controls", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(adapter->setProperty("sourceActive", true));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "play"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "pause"));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 300)));
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.position, 0);
    QVERIFY(adapter->setProperty("sourceMode", false));
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback")); QCOMPARE(legacy.playCalls, 1);
    legacy.playing = true;
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "togglePlayback")); QCOMPARE(legacy.pauseCalls, 1);
    QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 500))); QCOMPARE(legacy.position, 500);
    QCOMPARE(controls.sought, 640);
}

void OriginalUiPlaybackQmlTest::smtcUsesSourceStateAndIgnoresLegacyEvents()
{
    QQmlEngine engine; PlaybackControllerDouble controls; LegacyPlayerDouble legacy;
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"controls", QVariant::fromValue<QObject *>(&controls)},
        {"legacyPlayer", QVariant::fromValue<QObject *>(&legacy)},
        {"sourceMode", true}, {"sourceActive", true}, {"legacyActive", true}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/main.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.indexOf("    function updateSmtcControls()");
    const auto end = source.indexOf("    // 播放列表\n    LegacyQueueController", start);
    QVERIFY(start >= 0 && end > start);
    auto body = source.mid(start, end - start);
    // Execute the production functions and Connections, substituting only the native service.
    const auto nativeStart = body.indexOf("    WindowsSmtcManager {");
    const auto nativeEnd = body.indexOf("    Connections {", nativeStart);
    QVERIFY(nativeStart >= 0 && nativeEnd > nativeStart);
    body.remove(nativeStart, nativeEnd - nativeStart);
    body.replace("WindowsSmtcManager.", "smtcStates.");
    const auto fixture = QStringLiteral(R"(import QtQuick
import QtMultimedia
Item {
    id: window
    property var lyricsAdapter
    property var playbackAdapter
    property var smtcStates
    property bool sourceLyricsMode: true
    property bool securePlaybackCurrent: true
    property string currentCover: "file:///fixture/cache/source.png"
    property string musicTitle: "Legacy title"
    property string musicArtist: "Legacy artist"
    property QtObject playbackCoordinator: QtObject {
        property var queue: [{}, {}]
        property int currentIndex: 1
        property string currentOccurrence: "opaque-occurrence-A"
        property var currentItem: ({title: "Source title", artists: ["Source artist", "Other"], album: "Source album",
                                    ref: {accountId: "private-account"}, streamUrl: "https://private.invalid/audio"})
    }
    property QtObject playListModel: QtObject {
        property int count: 2; property int playListIndex: 0; property int reads: 0
        function get(index) { ++reads; return {path: "private-legacy-path"}; }
    }
    property QtObject musicControlMin: QtObject {
        property int nextCalls: 0; property int previousCalls: 0
        function enterMedia() { ++nextCalls; } function lastMedia() { ++previousCalls; }
    }
    property QtObject mainMedia: QtObject {
        property string album: "Legacy album"; property string urlStr: "https://legacy.invalid/cover"
        property int position: 9000; property int duration: 99000; property int playbackState: MediaPlayer.PlayingState
        signal sourceChanged()
    }
    property QtObject windowsSmtc: QtObject {
        property bool available: true
        property var metadata: []; property var timeline: []; property var enabledControls: []
        property int status: -1; property int calls: 0
        signal playPressed(); signal pausePressed(); signal nextPressed(); signal previousPressed(); signal seekRequested(int pos)
        function setControlsEnabled(play, pause, next, previous) { ++calls; enabledControls = [play, pause, next, previous]; }
        function setPlaybackStatus(value) { ++calls; status = value; }
        function updateMediaInfo(title, artist, album, cover, mediaId) { ++calls; metadata = [title, artist, album, cover, mediaId]; }
        function updateTimeline(position, duration) { ++calls; timeline = [position, duration]; }
    }
    function lateLegacySignals() {
        mainMedia.sourceChanged(); mainMedia.duration = 88888;
        mainMedia.position = 77777; mainMedia.playbackState = MediaPlayer.PausedState;
    }
%1
})").arg(body);
    const QVariantMap states{{"Closed", int(WindowsSmtcManager::Closed)}, {"Changing", int(WindowsSmtcManager::Changing)},
        {"Stopped", int(WindowsSmtcManager::Stopped)}, {"Playing", int(WindowsSmtcManager::Playing)}, {"Paused", int(WindowsSmtcManager::Paused)}};
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/smtc-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({
        {"lyricsAdapter", QVariant::fromValue(bridge.get())}, {"playbackAdapter", QVariant::fromValue<QObject *>(&controls)},
        {"smtcStates", states}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *smtc = root->property("windowsSmtc").value<QObject *>(); QVERIFY(smtc);
    auto *queue = root->property("playListModel").value<QObject *>(); QVERIFY(queue);
    auto *coordinator = root->property("playbackCoordinator").value<QObject *>(); QVERIFY(coordinator);
    auto *navigation = root->property("musicControlMin").value<QObject *>(); QVERIFY(navigation);
    const auto refresh = [&] { return QMetaObject::invokeMethod(root.get(), "syncSmtcPlayback"); };
    QVERIFY(refresh());
    QCOMPARE(smtc->property("metadata").toList(), QVariantList({"Source title", "Source artist, Other", "Source album",
                                                             "file:///fixture/cache/source.png", "opaque-occurrence-A"}));
    QCOMPARE(queue->property("reads").toInt(), 0);
    QCOMPARE(smtc->property("enabledControls").toList(), QVariantList({true, true, true, true}));
    QCOMPARE(smtc->property("timeline").toList(), QVariantList({100, 1000}));
    for (const auto &[state, status] : QList<QPair<int, int>>{
             {QtPlaybackController::Idle, WindowsSmtcManager::Closed},
             {QtPlaybackController::Loading, WindowsSmtcManager::Changing},
             {QtPlaybackController::Playing, WindowsSmtcManager::Playing},
             {QtPlaybackController::Paused, WindowsSmtcManager::Paused},
             {QtPlaybackController::Stopped, WindowsSmtcManager::Stopped},
             {QtPlaybackController::Error, WindowsSmtcManager::Stopped}}) {
        controls.state = state; emit controls.stateChanged();
        QCOMPARE(smtc->property("status").toInt(), status);
    }
    controls.position = 640; emit controls.positionChanged();
    controls.duration = 1200; emit controls.durationChanged();
    QCOMPARE(smtc->property("timeline").toList(), QVariantList({640, 1200}));
    const auto before = smtc->property("calls").toInt();
    QVERIFY(QMetaObject::invokeMethod(root.get(), "lateLegacySignals"));
    QCOMPARE(smtc->property("calls").toInt(), before);
    QVERIFY(QMetaObject::invokeMethod(smtc, "playPressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "pausePressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "seekRequested", Q_ARG(int, 500)));
    QCOMPARE(controls.playCalls, 1); QCOMPARE(controls.pauseCalls, 1); QCOMPARE(controls.sought, 500);
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.pauseCalls, 0); QCOMPARE(legacy.position, 0);
    QVERIFY(QMetaObject::invokeMethod(smtc, "nextPressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "previousPressed"));
    QCOMPARE(navigation->property("nextCalls").toInt(), 1); QCOMPARE(navigation->property("previousCalls").toInt(), 1);
    QVERIFY(root->setProperty("currentCover", "qrc:/default.png"));
    QCOMPARE(smtc->property("metadata").toList().at(3).toString(), QString());
    QVERIFY(coordinator->setProperty("currentIndex", 0));
    QVERIFY(coordinator->setProperty("queue", QVariantList{QVariantMap{}})); QVERIFY(refresh());
    QCOMPARE(smtc->property("enabledControls").toList(), QVariantList({true, true, false, false}));
    QVERIFY(root->setProperty("playbackAdapter", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(bridge->setProperty("controls", QVariant::fromValue<QObject *>(nullptr)));
    QCOMPARE(smtc->property("enabledControls").toList(), QVariantList({false, false, false, false}));
    QCOMPARE(smtc->property("timeline").toList(), QVariantList({0, 0}));
    QCOMPARE(smtc->property("status").toInt(), int(WindowsSmtcManager::Closed));
    QVERIFY(QMetaObject::invokeMethod(smtc, "playPressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "pausePressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "seekRequested", Q_ARG(int, 900)));
    QCOMPARE(controls.playCalls, 1); QCOMPARE(controls.pauseCalls, 1); QCOMPARE(controls.sought, 500);
    QCOMPARE(legacy.playCalls, 0); QCOMPARE(legacy.pauseCalls, 0); QCOMPARE(queue->property("reads").toInt(), 0);
    QVERIFY(root->setProperty("playbackAdapter", QVariant::fromValue<QObject *>(&controls)));
    QVERIFY(bridge->setProperty("controls", QVariant::fromValue<QObject *>(&controls)));
    QVERIFY(root->setProperty("securePlaybackCurrent", false));
    QVERIFY(bridge->setProperty("sourceActive", false)); QVERIFY(refresh());
    QCOMPARE(smtc->property("metadata").toList(), QVariantList({"", "", "", "", ""}));
    QCOMPARE(smtc->property("timeline").toList(), QVariantList({0, 0}));
    QCOMPARE(smtc->property("enabledControls").toList(), QVariantList({false, false, false, false}));
    QCOMPARE(smtc->property("status").toInt(), int(WindowsSmtcManager::Closed));
    QVERIFY(QMetaObject::invokeMethod(smtc, "playPressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "pausePressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "seekRequested", Q_ARG(int, 800)));
    QVERIFY(QMetaObject::invokeMethod(smtc, "nextPressed"));
    QCOMPARE(controls.playCalls, 1); QCOMPARE(controls.pauseCalls, 1); QCOMPARE(controls.sought, 500);
    QCOMPARE(navigation->property("nextCalls").toInt(), 1);
    QVERIFY(root->setProperty("sourceLyricsMode", false));
    QVERIFY(bridge->setProperty("sourceMode", false));
    QVERIFY(QMetaObject::invokeMethod(smtc, "playPressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "pausePressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "seekRequested", Q_ARG(int, 300)));
    QCOMPARE(legacy.playCalls, 1); QCOMPARE(legacy.pauseCalls, 1); QCOMPARE(legacy.position, 300);
    QVERIFY(queue->property("reads").toInt() > 0);
    QCOMPARE(smtc->property("metadata").toList().at(4).toString(), QString("private-legacy-path"));
    QVERIFY(smtc->setProperty("available", false));
    const auto unavailableCalls = smtc->property("calls").toInt(); QVERIFY(refresh());
    QCOMPARE(smtc->property("calls").toInt(), unavailableCalls);
    QVERIFY(QMetaObject::invokeMethod(smtc, "playPressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "pausePressed"));
    QVERIFY(QMetaObject::invokeMethod(smtc, "seekRequested", Q_ARG(int, 999)));
    QCOMPARE(legacy.playCalls, 1); QCOMPARE(legacy.pauseCalls, 1); QCOMPARE(legacy.position, 300);
    QVERIFY(smtc->setProperty("available", true));
    QVERIFY(smtc->property("calls").toInt() > unavailableCalls);
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

void OriginalUiPlaybackQmlTest::actualInfoDialogUsesOnlyCurrentDisplayFields()
{
    QQmlEngine engine; PlaybackControllerDouble controls;
    controls.duration = 65000;
    const QVariantMap sourceItem{{"title", "Source song"}, {"artists", QStringList{"One", "Two"}},
        {"album", "Source album"}, {"sourceLabel", "Office library"}, {"fileName", "private-file"},
        {"date", "private-date"}, {"format", "private-format"}, {"streamUrl", "https://private.invalid/audio"},
        {"ref", QVariantMap{{"accountId", "private-account"}}}, {"metadata", QVariantMap{{"format", "private"}}}};
    const QVariantMap legacy{{"title", "Legacy song"}, {"artist", "Legacy artist"}, {"album", "Legacy album"},
        {"fileName", "Legacy file"}, {"date", "Legacy date"}, {"format", "Legacy format"}};
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"controls", QVariant::fromValue<QObject *>(&controls)}, {"sourceMode", true}, {"sourceActive", true},
        {"sourceItem", sourceItem}, {"legacyDetails", legacy}, {"legacyDuration", 10000}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    const QVariantMap expected{{"title", "Source song"}, {"artist", "One, Two"}, {"album", "Source album"},
        {"sourceLabel", "Office library"}, {"fileName", ""}, {"date", ""}, {"format", ""}};
    QCOMPARE(bridge->property("details").toMap(), expected);
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto extract = [&](qsizetype start) {
        int depth = 0; auto end = start;
        for (; end < source.size(); ++end) {
            if (source[end] == '{') ++depth;
            else if (source[end] == '}' && --depth == 0) { ++end; break; }
        }
        return source.mid(start, end - start);
    };
    const auto dialogStart = source.lastIndexOf("QOptionDialog {", source.indexOf("id: playerInfoDialog"));
    const auto timeStart = source.indexOf("function formatTime(ms)");
    QVERIFY(dialogStart >= 0 && timeStart >= 0);
    auto fixture = QStringLiteral(R"(import QtQuick
Item {
    id: musicControlMin
    property var presentation
    property real currentDuration: presentation.duration
    property var styleFixture: ({settings: {textmain: 14}, themes: {textColor: "#222222", themeColor: "#00ff00"}})
    property QtObject window: QtObject {
        property var lyricsAdapter: musicControlMin.presentation
        property bool sourceLyricsMode: musicControlMin.presentation.sourceMode
    }
    component QOptionDialog: Item {
        property string title; property real dialogContentHeight; property alias options: optionHost.data
        Item { id: optionHost }
    }
    component SettingItem: Item { property string label; property real controlWidth }
    readonly property var fieldTexts: playerInfoDialog.options[0].children.map(row => row.children[0].text)
    readonly property var fieldLabels: playerInfoDialog.options[0].children.map(row => row.label)
%1
%2
})").arg(extract(timeStart), extract(dialogStart));
    fixture.replace("Style.", "musicControlMin.styleFixture.");
    QVERIFY(!extract(dialogStart).contains("mainMedia."));
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/info-dialog-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    QVERIFY(root->findChild<QObject *>("currentPlaybackInfoDialog"));
    QCOMPARE(root->property("fieldTexts").toList(), QVariantList({"Office library", "Source song", "One, Two", "Source album", "1:5", "未知", "未知"}));
    QCOMPARE(root->property("fieldLabels").toList().first().toString(), QString("来源："));
    QVERIFY(bridge->setProperty("sourceItem", QVariantMap{{"title", "Next song"}, {"artists", QStringList{"Next artist"}}}));
    controls.duration = 0; emit controls.durationChanged();
    QCOMPARE(root->property("fieldTexts").toList(), QVariantList({"未知", "Next song", "Next artist", "未知", "未知", "未知", "未知"}));
    QVERIFY(bridge->setProperty("sourceActive", false));
    QCOMPARE(root->property("fieldTexts").toList(), QVariantList({"未知", "未知", "未知", "未知", "未知", "未知", "未知"}));
    QVERIFY(bridge->setProperty("sourceActive", true));
    QVERIFY(bridge->setProperty("sourceItem", QVariant()));
    QCOMPARE(bridge->property("details").toMap().size(), 7);
    QCOMPARE(root->property("fieldTexts").toList(), QVariantList({"未知", "未知", "未知", "未知", "未知", "未知", "未知"}));
    QVERIFY(bridge->setProperty("sourceMode", false));
    QCOMPARE(root->property("fieldTexts").toList(), QVariantList({"Legacy file", "Legacy song", "Legacy artist", "Legacy album", "0:10", "Legacy date", "Legacy format"}));
    QCOMPARE(root->property("fieldLabels").toList().first().toString(), QString("文件名："));
}

void OriginalUiPlaybackQmlTest::actualDownloadButtonNeverFallsBackAfterSourceStops()
{
    QQmlEngine engine;
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.lastIndexOf("SButton {", source.indexOf("id: currentDownloadButton")); QVERIFY(start >= 0);
    int depth = 0, end = start;
    for (; end < source.size(); ++end) {
        if (source[end] == '{') ++depth;
        else if (source[end] == '}' && --depth == 0) { ++end; break; }
    }
    auto fixture = QStringLiteral(R"(import QtQuick
Item {
    id: musicControlMin
    property bool securePlaybackActive: false
    property var styleFixture: ({themes: {textColor: "#222222", hoverColor: "#cccccc"}})
    property QtObject window: QtObject {
        property bool sourceLyricsMode: true; property bool securePlaybackCurrent: false
        property QtObject lyricsAdapter: QtObject { property bool downloadEnabled: false; property var download: ({}) }
    }
    property QtObject playListModel: QtObject {
        property int playListIndex: 0; property int count: 1; property var trace: ({reads: 0})
        property int source: 0; property string path: "legacy-hash"
        function get(index) { ++trace.reads; return {source: source, path: path}; }
    }
    property QtObject apiFixture: QtObject {
        property int requests: 0; property string requestedPath: ""; property int requestedMode: -1
        function getMusicInfo(path, mode) { ++requests; requestedPath = path; requestedMode = mode; }
    }
    component SButton: Item { signal clicked(); property string iconCharacter; property real radius;
        property color buttonColor; property color hoverColor; property color iconColor;
        property bool shadowEnabled; property bool hovered: false }
    component QTip: Item { property string text }
    component FileDialog: Item {
        visible: false; property string title; property int fileMode; property list<string> nameFilters
        property url selectedFile; signal accepted(); signal rejected()
        function open() { visible = true; } function close() { visible = false; }
    }
%1
})").arg(source.mid(start, end - start));
    fixture.replace("Style.themes", "musicControlMin.styleFixture.themes");
    fixture.replace("MusicApi.", "musicControlMin.apiFixture.");
    fixture.replace("FileDialog.SaveFile", "1");
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/download-button-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
    auto *button = root->findChild<QObject *>("currentPlaybackDownloadButton"); QVERIFY(button);
    auto *mode = root->property("window").value<QObject *>(); QVERIFY(mode);
    auto *queue = root->property("playListModel").value<QObject *>(); QVERIFY(queue);
    auto *api = root->property("apiFixture").value<QObject *>(); QVERIFY(api);
    QVERIFY(!button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(root->setProperty("securePlaybackActive", true));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(root->setProperty("securePlaybackActive", false));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(queue->property("trace").toMap().value("reads").toInt(), 0); QCOMPARE(api->property("requests").toInt(), 0);
    QVERIFY(mode->setProperty("sourceLyricsMode", false));
    QVERIFY(button->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(api->property("requests").toInt(), 1); QCOMPARE(api->property("requestedPath").toString(), QString("legacy-hash"));
    QCOMPARE(api->property("requestedMode").toInt(), 1);
    QVERIFY(queue->setProperty("playListIndex", -1));
    QVERIFY(!button->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(queue->setProperty("playListIndex", 1));
    QVERIFY(!button->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(queue->setProperty("source", -1)); QVERIFY(queue->setProperty("playListIndex", 0));
    QVERIFY(!button->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(api->property("requests").toInt(), 1);
}

void OriginalUiPlaybackQmlTest::actualSourceDownloadDialogKeepsTheCapturedPlaybackToken()
{
    QQmlEngine engine; LyricsAdapterDouble music;
    music.download = {{"canDownload", true}, {"pending", false}, {"failed", false}, {"completed", false}, {"token", "A"}};
    QQmlComponent bridgeComponent(&engine, QUrl("qrc:/QueMusic/components/PlaybackLyricsAdapter.qml"));
    std::unique_ptr<QObject> bridge(bridgeComponent.createWithInitialProperties({
        {"musicAdapter", QVariant::fromValue<QObject *>(&music)}, {"sourceMode", true}, {"sourceActive", true}}));
    QVERIFY2(bridge, qPrintable(bridgeComponent.errorString()));
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    const auto start = source.lastIndexOf("SButton {", source.indexOf("id: currentDownloadButton")); QVERIFY(start >= 0);
    int depth = 0, end = start;
    for (; end < source.size(); ++end) {
        if (source[end] == '{') ++depth;
        else if (source[end] == '}' && --depth == 0) { ++end; break; }
    }
    auto fixture = QStringLiteral(R"(import QtQuick
Item {
    id: musicControlMin
    property var presentation
    property var styleFixture: ({themes: {textColor: "#222222", hoverColor: "#cccccc"}})
    property var playListModel: ({playListIndex: -1, count: 0})
    property QtObject window: QtObject {
        property bool sourceLyricsMode: musicControlMin.presentation.sourceMode
        property bool securePlaybackCurrent: musicControlMin.presentation.sourceActive
        property var lyricsAdapter: musicControlMin.presentation
    }
    component SButton: Item { signal clicked(); property string iconCharacter; property real radius;
        property color buttonColor; property color hoverColor; property color iconColor;
        property bool shadowEnabled; property bool hovered: false }
    component QTip: Item { property string text }
    component FileDialog: Item {
        visible: false; property string title; property int fileMode; property list<string> nameFilters
        property url selectedFile; property int openCalls: 0; property int closeCalls: 0
        signal accepted(); signal rejected()
        function open() { ++openCalls; visible = true; } function close() { ++closeCalls; visible = false; }
    }
%1
})").arg(source.mid(start, end - start));
    fixture.replace("Style.themes", "musicControlMin.styleFixture.themes");
    fixture.replace("FileDialog.SaveFile", "1");
    QQmlComponent component(&engine); component.setData(fixture.toUtf8(), QUrl("qrc:/source-download-dialog-fixture.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"presentation", QVariant::fromValue(bridge.get())}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *button = root->findChild<QObject *>("currentPlaybackDownloadButton"); QVERIFY(button);
    auto *dialog = root->findChild<QObject *>("currentPlaybackDownloadDestinationDialog"); QVERIFY(dialog);
    const auto target = QUrl::fromLocalFile("/fixture/user-selected.audio");
    QVERIFY(button->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(dialog->property("playbackToken").toString(), QString("A")); QCOMPARE(music.downloadCalls, 0);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked")); QCOMPARE(dialog->property("openCalls").toInt(), 1);
    QVERIFY(dialog->setProperty("selectedFile", target));
    music.download["token"] = "B"; emit music.changed();
    QVERIFY(dialog->property("closeCalls").toInt() > 0); QVERIFY(!dialog->property("visible").toBool());
    QVERIFY(dialog->property("playbackToken").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted")); QCOMPARE(music.downloadCalls, 0);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(dialog->property("playbackToken").toString(), QString("B"));
    QVERIFY(dialog->setProperty("selectedFile", target)); QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
    QCOMPARE(music.downloadCalls, 1); QCOMPARE(music.lastDownloadToken, QString("B")); QCOMPARE(music.lastDestination, target);
    QVERIFY(!button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted")); QCOMPARE(music.downloadCalls, 1);
    music.download["pending"] = false; music.download["failed"] = true; emit music.changed();
    QVERIFY(button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close")); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "rejected"));
    QVERIFY(dialog->property("playbackToken").toString().isEmpty()); QCOMPARE(music.downloadCalls, 1);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close")); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    music.download["canDownload"] = false; emit music.changed();
    QVERIFY(!button->property("enabled").toBool()); QVERIFY(!dialog->property("visible").toBool());
    QVERIFY(dialog->setProperty("selectedFile", target)); QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
    QCOMPARE(music.downloadCalls, 1);
    music.download["canDownload"] = true; emit music.changed();
    QVERIFY(QMetaObject::invokeMethod(button, "clicked")); QVERIFY(bridge->setProperty("sourceActive", false));
    QVERIFY(!dialog->property("visible").toBool()); QVERIFY(!button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted")); QCOMPARE(music.downloadCalls, 1);
    QVERIFY(bridge->setProperty("sourceActive", true)); QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(bridge->setProperty("musicAdapter", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(!dialog->property("visible").toBool()); QVERIFY(!button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted")); QCOMPARE(music.downloadCalls, 1);
    QVERIFY(bridge->setProperty("sourceMode", false));
    QVariant dispatched;
    QVERIFY(QMetaObject::invokeMethod(bridge.get(), "saveDownload", Q_RETURN_ARG(QVariant, dispatched),
                                      Q_ARG(QVariant, target), Q_ARG(QVariant, "B")));
    QVERIFY(!dispatched.toBool()); QCOMPARE(music.downloadCalls, 1);
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
    QVERIFY(source.contains(QStringLiteral("if (window.sourceLyricsMode) syncSmtcPlayback();")));
    QVERIFY(source.contains(QStringLiteral("function onQueueChanged() { if (window.sourceLyricsMode) updateSmtcControls(); }")));
    QVERIFY(source.contains(QStringLiteral("String(playbackCoordinator.currentOccurrence || \"\")")));
    QFile controlsFile(QStringLiteral(QUEMUSIC_SOURCE_DIR "/layout/PlayerControl.qml"));
    QVERIFY(controlsFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto controlsText = QString::fromUtf8(controlsFile.readAll());
    QCOMPARE(controlsText.count(QStringLiteral("if (window.sourceLyricsMode && !window.securePlaybackCurrent)")), 3);
    QVERIFY(controlsText.contains(QStringLiteral("onMoved: window.lyricsAdapter.seek(value)")));
    QVERIFY(controlsText.contains(QStringLiteral("enabled: window.sourceLyricsMode ? window.lyricsAdapter.favoriteEnabled")));
    QVERIFY(controlsText.contains(QStringLiteral("window.lyricsAdapter.setFavorite(values[index], playbackToken)")));
    QVERIFY(controlsText.contains(QStringLiteral("sourceFavoriteMenu.playbackToken = info.token")));
    QVERIFY(controlsText.contains(QStringLiteral("import QtQuick.Dialogs")));
    QVERIFY(controlsText.contains(QStringLiteral("enabled: window.sourceLyricsMode ? window.lyricsAdapter.downloadEnabled")));
    QVERIFY(controlsText.contains(QStringLiteral("window.lyricsAdapter.saveDownload(selectedFile, token)")));
    QVERIFY(controlsText.contains(QStringLiteral("sourceDownloadDialog.playbackToken = window.lyricsAdapter.download.token")));
    QVERIFY(controlsText.contains(QStringLiteral("sourceDownloadDialog.close(); sourceDownloadDialog.playbackToken = \"\";")));
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
