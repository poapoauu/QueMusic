#include "MediaBridge.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceManager.h"

#define private public
#include "SourceSessionRegistry.h"
#undef private

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

namespace {

class MemorySecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret, QString *error) override
    {
        Q_UNUSED(error)
        m_values.insert(reference, secret);
        return true;
    }

    std::optional<QByteArray> read(const QString &reference, QString *error) const override
    {
        const auto value = m_values.constFind(reference);
        if (value == m_values.cend()) {
            if (error != nullptr) {
                *error = QStringLiteral("Secret is unavailable");
            }
            return std::nullopt;
        }
        return *value;
    }

    bool remove(const QString &reference, QString *error) override
    {
        Q_UNUSED(error)
        m_values.remove(reference);
        return true;
    }

private:
    QHash<QString, QByteArray> m_values;
};

SourceAccount navidromeAccount(quint16 port)
{
    return {QStringLiteral("navidrome"),
            QStringLiteral("home"),
            QStringLiteral("Home server"),
            {{QStringLiteral("serverUrl"), QStringLiteral("http://127.0.0.1:%1").arg(port)},
             {QStringLiteral("username"), QStringLiteral("home-user")}},
            QByteArrayLiteral("test-password")};
}

QVariantMap trackItem(const QString &nativeId = QStringLiteral("song-1"))
{
    return {{QStringLiteral("sourceId"), QStringLiteral("navidrome")},
            {QStringLiteral("accountId"), QStringLiteral("home")},
            {QStringLiteral("nativeId"), nativeId},
            {QStringLiteral("kind"), static_cast<int>(MediaKind::Track)},
            {QStringLiteral("title"), QStringLiteral("Song")},
            {QStringLiteral("artists"), QStringList{QStringLiteral("Artist")}},
            {QStringLiteral("albumTitle"), QStringLiteral("Album")},
            {QStringLiteral("artworkUrl"), QUrl(QStringLiteral("https://art.example/song-1"))},
            {QStringLiteral("durationMs"), 12000}};
}

void sendJsonResponse(QTcpSocket *socket, const QByteArray &response)
{
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                  + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
    socket->disconnectFromHost();
}

class NavidromeFixture {
public:
    NavidromeFixture()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                const QByteArray request = socket->readAll();
                requestLines.append(request.split('\n').constFirst().trimmed());
                const QByteArray response = requestLines.constLast().contains("getSong.view")
                    ? QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"song\":{\"id\":\"song-1\",\"artist\":\"Artist\",\"title\":\"Song\"}}}")
                    : QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"lyrics\":{\"value\":\"line one\",\"synced\":false}}}");
                sendJsonResponse(socket, response);
            });
        });
    }

    void start()
    {
        QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    }

    QTcpServer server;
    QList<QByteArray> requestLines;
};

class NavidromeRegistryHarness {
public:
    NavidromeRegistryHarness()
        : settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")), QSettings::IniFormat)
        , accountStore(&settings, &secretStore)
        , sourceManager(&pluginManager)
        , registry(&sourceManager, &accountStore)
    {
        sourceManager.addSearchPath(
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../plugins")));
    }

    bool initialize(quint16 port)
    {
        return temporaryDirectory.isValid() && sourceManager.loadAll() == 1
            && accountStore.upsert(navidromeAccount(port));
    }

    QTemporaryDir temporaryDirectory;
    QSettings settings;
    MemorySecretStore secretStore;
    SourceAccountStore accountStore;
    PluginManager pluginManager;
    SourceManager sourceManager;
    SourceSessionRegistry registry;
};

class DeferredSession final : public IMusicSourceSession {
public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override { return {}; }
    QUuid browse(const BrowseQuery &) override { return {}; }
    QUuid resolveStream(const TrackRef &) override { return start(QStringLiteral("resolveStream")); }
    QUuid fetchArtwork(const TrackRef &) override { return start(QStringLiteral("fetchArtwork")); }
    QUuid fetchLyrics(const TrackRef &) override { return start(QStringLiteral("fetchLyrics")); }
    void cancel(const QUuid &requestId) override { cancelled.append(requestId); }

    void succeed(const QJsonObject &result)
    {
        succeed(requestId, result);
    }

    void succeed(const QUuid &completedRequestId, const QJsonObject &result)
    {
        emit requestSucceeded(completedRequestId, operations.value(completedRequestId), result);
    }

    QUuid requestId;
    QHash<QUuid, QString> operations;
    QList<QUuid> cancelled;
    int resolveStreamCalls = 0;

private:
    QUuid start(const QString &nextOperation)
    {
        requestId = QUuid::createUuid();
        operations.insert(requestId, nextOperation);
        if (nextOperation == QStringLiteral("resolveStream")) {
            ++resolveStreamCalls;
        }
        return requestId;
    }
};

DeferredSession *installSession(SourceSessionRegistry *registry, const MediaId &id)
{
    auto *session = new DeferredSession(registry);
    registry->m_sessions.insert(registry->keyFor(id), {session, {}});
    return session;
}

} // namespace

class MediaBridgePlaybackTest : public QObject {
    Q_OBJECT

private slots:
    void playEmitsUrlAuthenticatedPlaybackEntry();
    void loadArtworkUsesRegistryManagerForwarder();
    void loadLyricsEmitsNormalizedPayload();
    void playRejectsHeaderAuthenticatedStreams();
    void playSupersedesOlderPendingPlayback();
    void playPropagatesFutureExpiration();
    void playRejectsAlreadyExpiredStream();
    void enqueueDoesNotResolveStream();
    void cancellationAndInvalidationSuppressActionSignals();
};

void MediaBridgePlaybackTest::playEmitsUrlAuthenticatedPlaybackEntry()
{
    NavidromeRegistryHarness harness;
    QVERIFY(harness.initialize(8533));
    MediaBridge bridge(&harness.registry);
    QSignalSpy ready(&bridge, &MediaBridge::playbackReady);

    bridge.play(trackItem());

    QVERIFY(ready.wait(1000));
    const QVariantMap entry = ready.constFirst().at(0).toMap();
    QCOMPARE(entry.value(QStringLiteral("mediaId")).toMap().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    QCOMPARE(entry.value(QStringLiteral("title")).toString(), QStringLiteral("Song"));
    QCOMPARE(entry.value(QStringLiteral("artist")).toString(), QStringLiteral("Artist"));
    QCOMPARE(entry.value(QStringLiteral("albumTitle")).toString(), QStringLiteral("Album"));
    QCOMPARE(entry.value(QStringLiteral("durationMs")).toLongLong(), 12000);
    const QUrl url(entry.value(QStringLiteral("url")).toString());
    QVERIFY(url.path().endsWith(QStringLiteral("/rest/stream.view")));
    QVERIFY(QUrlQuery(url).hasQueryItem(QStringLiteral("t")));
    QVERIFY(QUrlQuery(url).hasQueryItem(QStringLiteral("s")));
    QVERIFY(!entry.contains(QStringLiteral("headers")));
}

void MediaBridgePlaybackTest::loadArtworkUsesRegistryManagerForwarder()
{
    NavidromeRegistryHarness harness;
    QVERIFY(harness.initialize(8533));
    MediaBridge bridge(&harness.registry);
    QSignalSpy ready(&bridge, &MediaBridge::artworkReady);

    bridge.loadArtwork(trackItem());

    QVERIFY(ready.wait(1000));
    const QVariantMap artwork = ready.constFirst().at(0).toMap();
    QCOMPARE(artwork.value(QStringLiteral("mediaId")).toMap().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    QVERIFY(artwork.value(QStringLiteral("artworkUrl")).toString().contains(
        QStringLiteral("/rest/getCoverArt.view")));
}

void MediaBridgePlaybackTest::loadLyricsEmitsNormalizedPayload()
{
    NavidromeFixture fixture;
    fixture.start();
    NavidromeRegistryHarness harness;
    QVERIFY(harness.initialize(fixture.server.serverPort()));
    MediaBridge bridge(&harness.registry);
    QSignalSpy ready(&bridge, &MediaBridge::lyricsReady);

    bridge.loadLyrics(trackItem());

    QVERIFY(ready.wait(1000));
    const QVariantMap lyrics = ready.constFirst().at(0).toMap();
    QCOMPARE(lyrics.value(QStringLiteral("mediaId")).toMap().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    QCOMPARE(lyrics.value(QStringLiteral("lyrics")).toString(), QStringLiteral("line one"));
    QVERIFY(!lyrics.value(QStringLiteral("synced")).toBool());
    QCOMPARE(fixture.requestLines.size(), 2);
    QVERIFY(fixture.requestLines.constFirst().contains("getSong.view"));
    QVERIFY(fixture.requestLines.constLast().contains("getLyrics.view"));
}

void MediaBridgePlaybackTest::playRejectsHeaderAuthenticatedStreams()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
                     MediaKind::Track};
    DeferredSession *session = installSession(&registry, id);
    MediaBridge bridge(&registry);
    QSignalSpy failed(&bridge, &MediaBridge::mediaActionFailed);

    bridge.play(trackItem());
    session->succeed({{QStringLiteral("url"), QStringLiteral("https://stream.example/song-1")},
                      {QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"),
                                                               QStringLiteral("Bearer token")}}}});

    bridge.play(trackItem());
    session->succeed({{QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"),
                                                               QStringLiteral("Bearer token")}}}});

    bridge.play(trackItem());
    session->succeed({{QStringLiteral("url"), QStringLiteral("https://stream.example/song-1")},
                      {QStringLiteral("headers"), QJsonArray{QStringLiteral("malformed")}}});

    QCOMPARE(failed.count(), 3);
    for (const QList<QVariant> &arguments : failed) {
        const QVariantMap error = arguments.at(0).toMap();
        QCOMPARE(error.value(QStringLiteral("action")).toString(), QStringLiteral("play"));
        QCOMPARE(error.value(QStringLiteral("kind")).toInt(),
                 static_cast<int>(MediaErrorKind::Unsupported));
    }
}

void MediaBridgePlaybackTest::playSupersedesOlderPendingPlayback()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
                     MediaKind::Track};
    DeferredSession *session = installSession(&registry, id);
    MediaBridge bridge(&registry);
    QSignalSpy ready(&bridge, &MediaBridge::playbackReady);

    bridge.play(trackItem(QStringLiteral("song-a")));
    const QUuid firstRequestId = session->requestId;
    bridge.play(trackItem(QStringLiteral("song-b")));
    const QUuid secondRequestId = session->requestId;

    QVERIFY(firstRequestId != secondRequestId);
    QCOMPARE(session->cancelled, QList<QUuid>{firstRequestId});
    session->succeed(firstRequestId,
                     {{QStringLiteral("url"), QStringLiteral("https://stream.example/song-a")}});
    QCOMPARE(ready.count(), 0);
    session->succeed(secondRequestId,
                     {{QStringLiteral("url"), QStringLiteral("https://stream.example/song-b")}});
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.constFirst().at(0).toMap().value(QStringLiteral("mediaId")).toMap()
                 .value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-b"));
}

void MediaBridgePlaybackTest::playPropagatesFutureExpiration()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
                     MediaKind::Track};
    DeferredSession *session = installSession(&registry, id);
    MediaBridge bridge(&registry);
    QSignalSpy ready(&bridge, &MediaBridge::playbackReady);
    const QDateTime expiresAt = QDateTime::currentDateTimeUtc().addSecs(60);

    bridge.play(trackItem());
    session->succeed({{QStringLiteral("url"), QStringLiteral("https://stream.example/song-1")},
                      {QStringLiteral("expiresAt"), expiresAt.toString(Qt::ISODateWithMs)}});

    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.constFirst().at(0).toMap().value(QStringLiteral("expiresAt")).toDateTime(), expiresAt);
}

void MediaBridgePlaybackTest::playRejectsAlreadyExpiredStream()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
                     MediaKind::Track};
    DeferredSession *session = installSession(&registry, id);
    MediaBridge bridge(&registry);
    QSignalSpy ready(&bridge, &MediaBridge::playbackReady);
    QSignalSpy failed(&bridge, &MediaBridge::mediaActionFailed);

    bridge.play(trackItem());
    session->succeed({{QStringLiteral("url"), QStringLiteral("https://stream.example/song-1")},
                      {QStringLiteral("expiresAt"),
                       QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs)}});

    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.constFirst().at(0).toMap().value(QStringLiteral("kind")).toInt(),
             static_cast<int>(MediaErrorKind::Unavailable));
}

void MediaBridgePlaybackTest::enqueueDoesNotResolveStream()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
                     MediaKind::Track};
    DeferredSession *session = installSession(&registry, id);
    MediaBridge bridge(&registry);
    QSignalSpy queued(&bridge, &MediaBridge::enqueueReady);

    bridge.enqueue(trackItem());

    QCOMPARE(queued.count(), 1);
    QCOMPARE(session->resolveStreamCalls, 0);
    const QVariantMap entry = queued.constFirst().at(0).toMap();
    QCOMPARE(entry.value(QStringLiteral("mediaId")).toMap().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    QVERIFY(!entry.contains(QStringLiteral("url")));
}

void MediaBridgePlaybackTest::cancellationAndInvalidationSuppressActionSignals()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home"), QStringLiteral("song-1"),
                     MediaKind::Track};
    DeferredSession *session = installSession(&registry, id);
    MediaBridge bridge(&registry);
    QSignalSpy ready(&bridge, &MediaBridge::playbackReady);

    bridge.play(trackItem());
    bridge.cancel();
    QCOMPARE(session->cancelled.size(), 1);
    session->succeed({{QStringLiteral("url"), QStringLiteral("https://stream.example/cancelled")}});
    QCOMPARE(ready.count(), 0);

    bridge.play(trackItem());
    emit registry.sessionInvalidated(id.sourceId, id.accountId);
    session->succeed({{QStringLiteral("url"), QStringLiteral("https://stream.example/invalidated")}});
    QCOMPARE(ready.count(), 0);
}

QTEST_MAIN(MediaBridgePlaybackTest)
#include "tst_MediaBridgePlayback.moc"
