#include "LegacyPlaybackRequestGuard.h"
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTest>
#include <functional>
#include <memory>

class LegacyRequestApi final : public QObject {
    Q_OBJECT
public:
    LegacyPlaybackRequestGuard guard;
    Q_INVOKABLE bool legacyPlaybackRequestIsCurrent(const QString &token) const { return guard.isCurrent(token); }
};
class LegacyRequestCoordinator final : public QObject {
    Q_OBJECT
public:
    int stops = 0;
    std::function<void()> onStop;
    Q_INVOKABLE void stop() { ++stops; const auto callback = onStop; if (callback) callback(); }
};
class LegacyRequestPlayer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool urlLocal MEMBER local)
    Q_PROPERTY(QString source MEMBER source)
    Q_PROPERTY(QString noTitle MEMBER title)
    Q_PROPERTY(QString urlStr MEMBER cover)
public:
    bool local = false;
    QString source, title, cover;
    int plays = 0;
    Q_INVOKABLE void play() { ++plays; }
};
class LegacyRequestQueue final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count)
    Q_PROPERTY(int playListIndex MEMBER currentIndex)
public:
    QVariantList rows{QVariantMap{{"path", "same-hash"}, {"source", 1}}};
    int currentIndex = 0;
    int count() const { return rows.size(); }
    Q_INVOKABLE QVariantMap get(int index) const { return rows.value(index).toMap(); }
    Q_INVOKABLE void append(const QVariantMap &row) { rows.append(row); }
};
class LegacyRequestCover final : public QObject {
    Q_OBJECT
public:
    int extractions = 0;
    Q_INVOKABLE void extractColorsFromUrl(const QString &) { ++extractions; }
};

static QString readSource(const QString &relative)
{
    QFile file(QStringLiteral(QUEMUSIC_SOURCE_DIR "/") + relative);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}

class LegacyPlaybackRequestsTest final : public QObject {
    Q_OBJECT
private slots:
    void intentIdentityAndSingleConsumption()
    {
        LegacyPlaybackRequestGuard guard;
        QVERIFY(!guard.take({}, "song", 0));
        const auto old = guard.begin("song", 0);
        const auto current = guard.begin("song", 0);
        QVERIFY(!old.isEmpty()); QVERIFY(current != old);
        QVERIFY(!guard.take(old, "song", 0));
        QVERIFY(!guard.take(current, "other-song", 0));
        QVERIFY(!guard.take(current, "song", 1));
        QVERIFY(guard.take(current, "song", 0));
        QVERIFY(!guard.take(current, "song", 0));
        QVERIFY(guard.isCurrent(current)); // Post-signal work may still use this intent.
        guard.invalidate();
        QVERIFY(!guard.isCurrent(current));
        QVERIFY(!guard.take(current, "song", 0));
        const auto next = guard.begin("song", 1);
        QVERIFY(guard.take(next, "song", 1));
        QVERIFY(guard.begin({}, 1).isEmpty()); QVERIFY(!guard.isCurrent(next));
        QVERIFY(guard.begin("song", 2).isEmpty());
    }

    void productionCallbackRejectsInvalidatedAndReentrantResults_data()
    {
        QTest::addColumn<int>("mutation");
        QTest::newRow("source-before-result") << 0;
        QTest::newRow("source-during-stop") << 1;
        QTest::newRow("new-legacy-during-stop") << 2;
        QTest::newRow("new-intent-in-earlier-listener") << 3;
        QTest::newRow("explicit-legacy-intent") << 4;
    }
    void productionCallbackRejectsInvalidatedAndReentrantResults()
    {
        QFETCH(int, mutation);
        QQmlEngine engine;
        LegacyRequestApi api; LegacyRequestCoordinator coordinator;
        LegacyRequestPlayer player; LegacyRequestQueue queue; LegacyRequestCover cover;
        auto *context = engine.rootContext();
        context->setContextProperty("MusicApi", &api);
        context->setContextProperty("playbackCoordinator", &coordinator);
        context->setContextProperty("mainMedia", &player);
        context->setContextProperty("playListModel", &queue);
        context->setContextProperty("colorExtractor", &cover);
        const auto main = readSource("main.qml");
        const auto start = main.indexOf("function onUrlplay(");
        const auto end = main.indexOf("// C++ 下载/提示信号", start);
        QVERIFY(start >= 0 && end > start);
        QQmlComponent component(&engine);
        component.setData((QStringLiteral("import QtQml\nQtObject { id: window; "
            "property bool sourceLyricsMode: true; property string musicTitle: 'Source'; "
            "property string musicArtist: 'Source artist';\n")
            + main.mid(start, end - start) + "\n}").toUtf8(), QUrl{});
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        const auto token = api.guard.begin("same-hash", 0);
        QVERIFY(api.guard.take(token, "same-hash", 0)); // Service consumes before emitting.
        if (mutation == 0) api.guard.invalidate();
        if (mutation == 1) coordinator.onStop = [&] { api.guard.invalidate(); };
        if (mutation == 2) coordinator.onStop = [&] { api.guard.begin("same-hash", 0); };
        if (mutation == 3) api.guard.begin("same-hash", 0);
        QVERIFY(QMetaObject::invokeMethod(window.get(), "onUrlplay",
            Q_ARG(QVariant, QVariant("https://fixture.invalid/audio")),
            Q_ARG(QVariant, QVariant("Legacy title")), Q_ARG(QVariant, QVariant("Legacy artist")),
            Q_ARG(QVariant, QVariant("cover")), Q_ARG(QVariant, QVariant("thumbnail")),
            Q_ARG(QVariant, QVariant("same-hash")), Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(token))));
        QCOMPARE(coordinator.stops, mutation == 0 || mutation == 3 ? 0 : 1);
        QCOMPARE(player.plays, mutation == 4 ? 1 : 0);
        QCOMPARE(cover.extractions, mutation == 4 ? 1 : 0);
        QCOMPARE(queue.count(), mutation == 4 ? 2 : 1); // Equal hashes in another platform are distinct.
        QCOMPARE(window->property("sourceLyricsMode").toBool(), mutation != 4);
        QCOMPARE(window->property("musicTitle").toString(), mutation == 4 ? QString("Legacy title") : QString("Source"));
        if (mutation != 4) QVERIFY(player.source.isEmpty());
        else QCOMPARE(queue.currentIndex, 1);
    }
    void providerAndHostWiringPreservesCapturedToken()
    {
        const auto service = readSource("api/MusicApiService.cpp");
        QVERIFY(service.contains("getMusicInfo(hash, type, playbackRequest)"));
        const auto take = service.indexOf("m_legacyPlayback.take(token,");
        const auto emitOffset = service.indexOf("emit urlplay(");
        QVERIFY(take >= 0 && emitOffset > take);
        QVERIFY(service.contains("source, token);"));
        QVERIFY(service.contains("if (m_legacyPlayback.isCurrent(token))"));
        const auto kugou = readSource("api/KugouApi.cpp");
        QVERIFY(kugou.contains("[this, hash, type, playbackRequest]"));
        QVERIFY(kugou.contains("data.insert(QStringLiteral(\"_legacyPlaybackRequest\"), playbackRequest)"));
        const auto netease = readSource("api/NeteaseCloudApi.cpp");
        QVERIFY(netease.contains("call.insert(QStringLiteral(\"_legacyPlaybackRequest\"), playbackRequest)"));
        QVERIFY(netease.contains("d.insert(QStringLiteral(\"_legacyPlaybackRequest\"), call.value("));
        const auto main = readSource("main.qml");
        const auto callback = main.mid(main.indexOf("onSecurePlaybackCurrentChanged:"), 240);
        QVERIFY(callback.contains("MusicApi.invalidateLegacyPlayback();"));
        const auto sync = main.mid(main.indexOf("function syncSecureCurrent()"), 400);
        QVERIFY(sync.indexOf("invalidateLegacyPlayback") < sync.indexOf("mainMedia.stop()"));
    }
};
QTEST_MAIN(LegacyPlaybackRequestsTest)
#include "tst_LegacyPlaybackRequests.moc"
