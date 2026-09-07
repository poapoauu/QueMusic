#include "NavidromeSourceSession.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrlQuery>

namespace {

QByteArray subsonicResponse(const QJsonObject &fields)
{
    QJsonObject response{{QStringLiteral("status"), QStringLiteral("ok")},
                         {QStringLiteral("version"), QStringLiteral("1.16.1")},
                         {QStringLiteral("type"), QStringLiteral("Navidrome")},
                         {QStringLiteral("serverVersion"), QStringLiteral("0.59.0")},
                         {QStringLiteral("openSubsonic"), true}};
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        response.insert(it.key(), it.value());
    return QJsonDocument(QJsonObject{{QStringLiteral("subsonic-response"), response}})
        .toJson(QJsonDocument::Compact);
}

bool available(const CapabilitySetV2 &capabilities, SourceActionV2 action)
{
    return capabilities.serverAction(action).state == AvailabilityV2::Available
        && capabilities.accountAction(action).state == AvailabilityV2::Available
        && capabilities.action(action).state == AvailabilityV2::Available;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream errors(stderr);
    QTcpServer server;
    QHash<QTcpSocket *, QByteArray> requestBuffers;
    const QString username = QStringLiteral("smoke-user");
    const QStringList expectedPaths{
        QStringLiteral("/rest/ping.view"),
        QStringLiteral("/rest/getOpenSubsonicExtensions.view"),
        QStringLiteral("/rest/getUser.view")};
    QList<QByteArray> responses{
        subsonicResponse({}),
        subsonicResponse({
            {QStringLiteral("openSubsonicExtensions"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("lyrics")},
                             {QStringLiteral("versions"), QJsonArray{1}}},
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("songLyrics")},
                             {QStringLiteral("versions"), QJsonArray{1}}}}}}),
        subsonicResponse({
            {QStringLiteral("user"),
             QJsonObject{{QStringLiteral("username"), username},
                         {QStringLiteral("streamRole"), true},
                         {QStringLiteral("coverArtRole"), true},
                         {QStringLiteral("downloadRole"), true},
                         {QStringLiteral("playlistRole"), true}}}})};
    int requestCount = 0;
    QString failure;

    const auto fail = [&](const QString &message) {
        if (failure.isEmpty())
            failure = message;
        app.exit(1);
    };

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            requestBuffers.insert(socket, {});
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray &buffer = requestBuffers[socket];
                buffer += socket->readAll();
                if (!buffer.contains("\r\n\r\n"))
                    return;

                const QList<QByteArray> requestParts = buffer.split('\n').constFirst().trimmed().split(' ');
                if (requestParts.size() < 2 || requestCount >= expectedPaths.size()
                    || responses.isEmpty()) {
                    fail(QStringLiteral("unexpected HTTP request"));
                    socket->disconnectFromHost();
                    return;
                }

                const QUrl url = QUrl::fromEncoded(requestParts.at(1));
                const QUrlQuery query(url);
                if (url.path() != expectedPaths.at(requestCount)
                    || query.queryItemValue(QStringLiteral("u")) != username
                    || query.hasQueryItem(QStringLiteral("p"))
                    || !query.hasQueryItem(QStringLiteral("t"))
                    || !query.hasQueryItem(QStringLiteral("s"))) {
                    fail(QStringLiteral("unexpected authenticated request"));
                    socket->disconnectFromHost();
                    return;
                }
                if (requestCount == 2
                    && query.queryItemValue(QStringLiteral("username")) != username) {
                    fail(QStringLiteral("getUser did not request the configured user"));
                    socket->disconnectFromHost();
                    return;
                }

                ++requestCount;
                const QByteArray response = responses.takeFirst();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(response.size())
                              + "\r\nConnection: close\r\n\r\n" + response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QObject::destroyed, &server,
                             [&, socket] { requestBuffers.remove(socket); });
        }
    });

    if (!server.listen(QHostAddress::LocalHost)) {
        errors << "failed to start local smoke server: " << server.errorString() << '\n';
        return 1;
    }

    SourceConfigurationV2 configuration{
        QStringLiteral("org.quemusic.source.navidrome"),
        QStringLiteral("navidrome"),
        QStringLiteral("navidrome/smoke"),
        QStringLiteral("smoke"),
        QStringLiteral("Navidrome smoke"),
        {{QStringLiteral("serverUrl"),
          QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())},
         {QStringLiteral("username"), username},
         {QStringLiteral("quality"), QStringLiteral("original")}},
        QByteArrayLiteral("test-password")};
    QNetworkAccessManager network;
    NavidromeSourceSession session(std::move(configuration), &network);
    bool requestStarted = false;

    QObject::connect(&session, &IMusicSourceSessionV2::requestStarted, &app,
                     [&](const QUuid &requestId) { requestStarted = !requestId.isNull(); });
    QObject::connect(&session, &IMusicSourceSessionV2::requestFailed, &app,
                     [&](const QUuid &, const SourceErrorV2 &error) {
                         fail(QStringLiteral("open failed (%1): %2")
                                  .arg(static_cast<int>(error.kind))
                                  .arg(error.detail));
                     });
    QObject::connect(&session, &IMusicSourceSessionV2::stateChanged, &app,
                     [&](SourceSessionStateV2 state) {
        if (state != SourceSessionStateV2::Ready)
            return;
        const CapabilitySetV2 capabilities = session.capabilities();
        if (requestCount != 3 || !available(capabilities, SourceActionV2::Play)
            || !available(capabilities, SourceActionV2::Artwork)
            || !available(capabilities, SourceActionV2::Lyrics)
            || !available(capabilities, SourceActionV2::Download)
            || capabilities.action(SourceActionV2::CreatePlaylist).state
                != AvailabilityV2::Available) {
            fail(QStringLiteral("open completed without negotiated capabilities"));
            return;
        }
        app.exit(0);
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app,
                     [&] { fail(QStringLiteral("open handshake timed out")); });
    timeout.start(5000);

    const QUuid openRequest = session.open();
    if (openRequest.isNull() || !requestStarted
        || session.state() != SourceSessionStateV2::Connecting) {
        errors << "open did not enter Connecting with a started request\n";
        return 1;
    }

    const int exitCode = app.exec();
    if (exitCode != 0)
        errors << failure << '\n';
    return exitCode;
}
