#include "IMusicSourceArtworkSession.h"
#include "NavidromeSourcePlugin.h"
#include "NavidromeSourceSession.h"

#include <QCryptographicHash>
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

QTEST_MAIN(NavidromeSourceTest)
#include "tst_NavidromeSource.moc"
