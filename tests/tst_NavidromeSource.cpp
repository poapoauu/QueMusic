#include "IMusicSourceArtworkSession.h"
#include "NavidromeSourcePlugin.h"
#include "NavidromeSourceSession.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {

SourceAccount navidromeAccount(quint16 port)
{
    return {QStringLiteral("navidrome"),
            QStringLiteral("admin"),
            QStringLiteral("Navidrome Admin"),
            {{QStringLiteral("serverUrl"), QStringLiteral("http://127.0.0.1:%1").arg(port)},
             {QStringLiteral("username"), QStringLiteral("admin")}},
            QByteArrayLiteral("test-password")};
}

} // namespace

class NavidromeSourceTest : public QObject {
    Q_OBJECT

private slots:
    void reportsNavidromeDescriptor();
    void createsConfiguredSession();
    void pingUsesSubsonicTokenAuthentication();
    void cancelSuppressesTerminalSignal();
    void searchMapsSongsAlbumsAndArtists();
    void browseMapsRootAndDirectoryResponses();
    void resolveStreamReturnsAuthenticatedStreamDto();
    void artworkUsesOptionalInterfaceAndReturnsArtworkDto();
    void lyricsFetchesSongMetadataThenLyrics();
    void mapsSubsonicErrorsAndMalformedJson();
    void cancelsLyricsSecondStageWithoutTerminalSignal();
    void lyricsUsesCachedSearchMetadata();
};

void NavidromeSourceTest::reportsNavidromeDescriptor()
{
    NavidromeSourcePlugin plugin;
    const SourceDescriptor descriptor = plugin.descriptor();

    QCOMPARE(descriptor.id, QStringLiteral("navidrome"));
    QCOMPARE(descriptor.protocol, QStringLiteral("subsonic"));
    QCOMPARE(descriptor.sdkVersion, QStringLiteral("1.0"));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Search));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Browse));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::StreamAudio));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Artwork));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Lyrics));
}

void NavidromeSourceTest::createsConfiguredSession()
{
    NavidromeSourcePlugin plugin;
    QNetworkAccessManager network;
    SourcePluginContext context{&network, QStringLiteral("/tmp/quemusic-navidrome-test")};
    const SourceAccount account{
        QStringLiteral("navidrome"),
        QStringLiteral("admin"),
        QStringLiteral("Navidrome Admin"),
        {{QStringLiteral("serverUrl"), QStringLiteral("http://example.invalid:8533")},
         {QStringLiteral("username"), QStringLiteral("admin")}},
        QByteArrayLiteral("test-password")};

    QVERIFY(plugin.initialize(context));
    IMusicSourceSession *session = plugin.createSession(account, &plugin);

    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &plugin);
    QVERIFY(qobject_cast<IMusicSourceArtworkSession *>(session) != nullptr);
}

void NavidromeSourceTest::pingUsesSubsonicTokenAuthentication()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));

    QByteArray capturedRequest;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            capturedRequest += socket->readAll();
            if (!capturedRequest.contains("\r\n\r\n")) {
                return;
            }

            const QByteArray response =
                "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\"}}";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);

    const QUuid requestId = session.ping();

    QVERIFY(!requestId.isNull());
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("ping"));
    QVERIFY(!capturedRequest.contains("test-password"));

    const QList<QByteArray> requestLine = capturedRequest.split('\n').constFirst().trimmed().split(' ');
    QCOMPARE(requestLine.size(), 3);
    QVERIFY(requestLine.at(1).startsWith("/rest/ping.view?"));

    const QUrl requestUrl(QStringLiteral("http://test.invalid") +
                          QString::fromUtf8(requestLine.at(1)));
    const QUrlQuery query(requestUrl);
    QCOMPARE(query.queryItemValue(QStringLiteral("u")), QStringLiteral("admin"));
    QCOMPARE(query.queryItemValue(QStringLiteral("v")), QStringLiteral("1.16.1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("c")), QStringLiteral("QueMusic"));
    QCOMPARE(query.queryItemValue(QStringLiteral("f")), QStringLiteral("json"));

    const QString salt = query.queryItemValue(QStringLiteral("s"));
    QVERIFY(salt.size() >= 6);
    const QString token = query.queryItemValue(QStringLiteral("t"));
    QCOMPARE(token, QString::fromLatin1(QCryptographicHash::hash(
                        QByteArrayLiteral("test-password") + salt.toUtf8(),
                        QCryptographicHash::Md5)
                        .toHex()));
}

void NavidromeSourceTest::cancelSuppressesTerminalSignal()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);
    QSignalSpy failed(&session, &IMusicSourceSession::requestFailed);

    const QUuid requestId = session.ping();
    session.cancel(requestId);

    QTest::qWait(100);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void NavidromeSourceTest::searchMapsSongsAlbumsAndArtists()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            const QByteArray response =
                "{\"subsonic-response\":{\"status\":\"ok\",\"searchResult3\":{"
                "\"song\":[{\"id\":\"song-1\",\"title\":\"Song\",\"artist\":\"Artist\","
                "\"album\":\"Album\",\"duration\":12,\"coverArt\":\"cover-1\"}],"
                "\"album\":[{\"id\":\"album-1\",\"name\":\"Album\",\"artist\":\"Artist\"}],"
                "\"artist\":[{\"id\":\"artist-1\",\"name\":\"Artist\"}]}}}";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);

    const QUuid requestId = session.search({QStringLiteral("Song"), 10});

    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("search"));
    const QJsonArray items = succeeded.constFirst().at(2).value<QJsonValue>()
                                 .toObject()
                                 .value(QStringLiteral("items"))
                                 .toArray();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("song-1"));
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("sourceId")).toString(), QStringLiteral("navidrome"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("album"));
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("artist"));
}

void NavidromeSourceTest::browseMapsRootAndDirectoryResponses()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QList<QByteArray> requestLines;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const QByteArray request = socket->readAll();
            const QByteArray requestLine = request.split('\n').constFirst().trimmed();
            requestLines.append(requestLine);
            const QByteArray response = requestLine.contains("getIndexes.view")
                ? QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"indexes\":{\"artist\":[{\"id\":\"artist-1\",\"name\":\"Artist\"}]}}}")
                : QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"directory\":{\"id\":\"album-1\",\"name\":\"Album\",\"child\":[{\"id\":\"song-1\",\"title\":\"Song\",\"artist\":\"Artist\"}]}}}");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);

    session.browse({});
    QVERIFY(succeeded.wait(1000));
    const QJsonArray rootItems = succeeded.constFirst().at(2).value<QJsonValue>()
                                     .toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(rootItems.at(0).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("artist"));

    session.browse({QStringLiteral("album-1")});
    QVERIFY(succeeded.wait(1000));
    const QJsonArray directoryItems = succeeded.constLast().at(2).value<QJsonValue>()
                                          .toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(directoryItems.at(0).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("track"));
    QVERIFY(requestLines.constFirst().contains("/rest/getIndexes.view?"));
    QVERIFY(requestLines.constLast().contains("/rest/getMusicDirectory.view?"));
    QVERIFY(requestLines.constLast().contains("id=album-1"));
}

void NavidromeSourceTest::resolveStreamReturnsAuthenticatedStreamDto()
{
    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(8533), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{QStringLiteral("navidrome"), QStringLiteral("song-1")};

    const QUuid requestId = session.resolveStream(track);

    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("resolveStream"));
    const QJsonObject result = succeeded.constFirst().at(2).value<QJsonValue>().toObject();
    QCOMPARE(result.value(QStringLiteral("track")).toObject().value(QStringLiteral("nativeId")).toString(),
             QStringLiteral("song-1"));
    const QString url = result.value(QStringLiteral("url")).toString();
    QVERIFY(url.contains(QStringLiteral("/rest/stream.view?")));
    QVERIFY(url.contains(QStringLiteral("id=song-1")));
    QVERIFY(!url.contains(QStringLiteral("test-password")));
    QVERIFY(result.value(QStringLiteral("seekable")).toBool());
}

void NavidromeSourceTest::artworkUsesOptionalInterfaceAndReturnsArtworkDto()
{
    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(8533), &network);
    auto *artwork = qobject_cast<IMusicSourceArtworkSession *>(&session);
    QVERIFY(artwork != nullptr);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);

    const QUuid requestId = artwork->fetchArtwork({QStringLiteral("navidrome"),
                                                    QStringLiteral("cover-1")});

    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("fetchArtwork"));
    const QJsonObject result = succeeded.constFirst().at(2).value<QJsonValue>().toObject();
    QVERIFY(result.value(QStringLiteral("url")).toString().contains(
        QStringLiteral("/rest/getCoverArt.view?")));
    QVERIFY(!result.value(QStringLiteral("url")).toString().contains(
        QStringLiteral("test-password")));
}

void NavidromeSourceTest::lyricsFetchesSongMetadataThenLyrics()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QList<QByteArray> requestLines;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const QByteArray request = socket->readAll();
            const QByteArray requestLine = request.split('\n').constFirst().trimmed();
            requestLines.append(requestLine);
            const QByteArray response = requestLine.contains("getSong.view")
                ? QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"song\":{\"id\":\"song-1\",\"artist\":\"Artist\",\"title\":\"Song\"}}}")
                : QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"lyrics\":{\"value\":\"line one\",\"synced\":false}}}");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{QStringLiteral("navidrome"), QStringLiteral("song-1")};

    const QUuid requestId = session.fetchLyrics(track);

    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.count(), 1);
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("fetchLyrics"));
    const QJsonObject result = succeeded.constFirst().at(2).value<QJsonValue>().toObject();
    QCOMPARE(result.value(QStringLiteral("lyrics")).toString(), QStringLiteral("line one"));
    QVERIFY(requestLines.constFirst().contains("/rest/getSong.view?"));
    QVERIFY(requestLines.constLast().contains("/rest/getLyrics.view?"));
    QVERIFY(requestLines.constLast().contains("artist=Artist"));
    QVERIFY(requestLines.constLast().contains("title=Song"));
}

void NavidromeSourceTest::mapsSubsonicErrorsAndMalformedJson()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    int responseCount = 0;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            socket->readAll();
            const QByteArray response = responseCount++ == 0
                ? QByteArray("{\"subsonic-response\":{\"status\":\"failed\",\"error\":{\"code\":40}}}")
                : QByteArray("not-json");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy failed(&session, &IMusicSourceSession::requestFailed);
    SourceError lastError;
    connect(&session, &IMusicSourceSession::requestFailed, this,
            [&lastError](const QUuid &, const SourceError &error) { lastError = error; });

    session.ping();
    QVERIFY(failed.wait(1000));
    QCOMPARE(lastError.kind, SourceErrorKind::Authentication);
    QCOMPARE(lastError.httpStatus, std::optional<int>(200));

    session.search({QStringLiteral("Song"), 1});
    QVERIFY(failed.wait(1000));
    QCOMPARE(lastError.kind, SourceErrorKind::InvalidRequest);
    QCOMPARE(lastError.httpStatus, std::optional<int>(200));
}

void NavidromeSourceTest::cancelsLyricsSecondStageWithoutTerminalSignal()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    int requestCount = 0;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            socket->readAll();
            if (requestCount++ != 0) {
                return;
            }
            const QByteArray response =
                "{\"subsonic-response\":{\"status\":\"ok\",\"song\":{\"id\":\"song-1\",\"artist\":\"Artist\",\"title\":\"Song\"}}}";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);
    QSignalSpy failed(&session, &IMusicSourceSession::requestFailed);

    const QUuid requestId = session.fetchLyrics({QStringLiteral("navidrome"),
                                                  QStringLiteral("song-1")});
    QTRY_VERIFY_WITH_TIMEOUT(requestCount == 2, 1000);
    session.cancel(requestId);

    QTest::qWait(100);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void NavidromeSourceTest::lyricsUsesCachedSearchMetadata()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QList<QByteArray> requestLines;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const QByteArray requestLine = socket->readAll().split('\n').constFirst().trimmed();
            requestLines.append(requestLine);
            const QByteArray response = requestLines.size() == 1
                ? QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"searchResult3\":{\"song\":[{\"id\":\"song-1\",\"title\":\"Song\",\"artist\":\"Artist\"}]}}}")
                : QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"lyrics\":{\"value\":\"line one\",\"synced\":false}}}");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(navidromeAccount(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);
    session.search({QStringLiteral("Song"), 1});
    QVERIFY(succeeded.wait(1000));
    succeeded.clear();

    session.fetchLyrics({QStringLiteral("navidrome"), QStringLiteral("song-1")});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(requestLines.size(), 2);
    QVERIFY(requestLines.at(1).contains("/rest/getLyrics.view?"));
    QVERIFY(!requestLines.at(1).contains("/rest/getSong.view?"));
}

QTEST_MAIN(NavidromeSourceTest)
#include "tst_NavidromeSource.moc"
