#include "NavidromeApiClient.h"
#include "NavidromeSourcePlugin.h"
#include "NavidromeSourceSession.h"
#include "v2/ISourceProvidersV2.h"
#include "v2/SourceSecretsV2.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <cstring>

namespace {

struct CapturedRequest {
    QByteArray requestLine;
    QUrl url;
    QList<QByteArray> headers;
};

class FakeNavidromeServer final : public QTcpServer {
public:
    struct Response {
        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        bool respond = true;
    };

    explicit FakeNavidromeServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    QByteArray bytes = socket->property("requestBytes").toByteArray();
                    bytes += socket->readAll();
                    socket->setProperty("requestBytes", bytes);
                    if (!bytes.contains("\r\n\r\n") || socket->property("handled").toBool())
                        return;
                    socket->setProperty("handled", true);
                    const QList<QByteArray> lines = bytes.left(bytes.indexOf("\r\n\r\n"))
                                                          .split('\n');
                    const QByteArray requestLine = lines.constFirst().trimmed();
                    const QList<QByteArray> parts = requestLine.split(' ');
                    CapturedRequest request;
                    request.requestLine = requestLine;
                    request.url = QUrl(QStringLiteral("http://fixture.invalid")
                                       + QString::fromUtf8(parts.value(1)));
                    for (qsizetype index = 1; index < lines.size(); ++index)
                        request.headers.append(lines.at(index).trimmed());
                    m_requests.append(request);
                    if (m_responses.isEmpty() || !m_responses.constFirst().respond)
                        return;
                    const Response response = m_responses.takeFirst();
                    const QByteArray statusText = response.status == 200 ? "OK" : "Error";
                    QByteArray wire = "HTTP/1.1 " + QByteArray::number(response.status) + " "
                                      + statusText + "\r\nContent-Type: application/json\r\n";
                    for (const auto &header : response.headers)
                        wire += header.first + ": " + header.second + "\r\n";
                    wire += "Content-Length: " + QByteArray::number(response.body.size())
                            + "\r\nConnection: close\r\n\r\n" + response.body;
                    socket->write(wire);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool start() { return listen(QHostAddress::LocalHost); }
    void enqueue(const QJsonObject &response, int status = 200)
    {
        m_responses.append({status, QJsonDocument(response).toJson(QJsonDocument::Compact),
                            {}, true});
    }
    void enqueueRaw(QByteArray body, int status = 200)
    {
        m_responses.append({status, std::move(body), {}, true});
    }
    void enqueueHeld() { m_responses.append({200, {}, {}, false}); }
    const QList<CapturedRequest> &requests() const { return m_requests; }

private:
    QList<Response> m_responses;
    QList<CapturedRequest> m_requests;
};

QJsonObject subsonicOk(QJsonObject fields = {})
{
    QJsonObject response{{QStringLiteral("status"), QStringLiteral("ok")},
                         {QStringLiteral("version"), QStringLiteral("1.16.1")},
                         {QStringLiteral("type"), QStringLiteral("Navidrome")},
                         {QStringLiteral("serverVersion"), QStringLiteral("0.59.0")},
                         {QStringLiteral("openSubsonic"), true}};
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        response.insert(it.key(), it.value());
    return {{QStringLiteral("subsonic-response"), response}};
}

QJsonObject extensionsResponse(const QStringList &names)
{
    QJsonArray extensions;
    for (const QString &name : names) {
        extensions.append(QJsonObject{{QStringLiteral("name"), name},
                                      {QStringLiteral("versions"), QJsonArray{1, 2}}});
    }
    return subsonicOk({{QStringLiteral("openSubsonicExtensions"), extensions}});
}

QJsonObject userResponse(QString username = QStringLiteral("admin"),
                         bool stream = true, bool coverArt = true,
                         bool download = true, bool playlist = true)
{
    return subsonicOk({{QStringLiteral("user"),
                         QJsonObject{{QStringLiteral("folder"), QJsonArray{1, 3}},
                                     {QStringLiteral("username"), username},
                                     {QStringLiteral("email"), QStringLiteral("admin@example.invalid")},
                                     {QStringLiteral("scrobblingEnabled"), false},
                                     {QStringLiteral("adminRole"), false},
                                     {QStringLiteral("settingsRole"), true},
                                     {QStringLiteral("downloadRole"), download},
                                     {QStringLiteral("uploadRole"), false},
                                     {QStringLiteral("playlistRole"), playlist},
                                     {QStringLiteral("coverArtRole"), coverArt},
                                     {QStringLiteral("commentRole"), false},
                                     {QStringLiteral("podcastRole"), false},
                                     {QStringLiteral("streamRole"), stream},
                                     {QStringLiteral("jukeboxRole"), false},
                                     {QStringLiteral("shareRole"), false},
                                     {QStringLiteral("videoConversionRole"), false}}}});
}

QJsonObject subsonicError(int code, const QString &message)
{
    return {{QStringLiteral("subsonic-response"),
             QJsonObject{{QStringLiteral("status"), QStringLiteral("failed")},
                         {QStringLiteral("version"), QStringLiteral("1.16.1")},
                         {QStringLiteral("type"), QStringLiteral("Navidrome")},
                         {QStringLiteral("serverVersion"), QStringLiteral("0.59.0")},
                         {QStringLiteral("openSubsonic"), true},
                         {QStringLiteral("error"),
                          QJsonObject{{QStringLiteral("code"), code},
                                      {QStringLiteral("message"), message}}}}}};
}

SourceConfigurationV2 configuration(quint16 port, QByteArray secret,
                                    QString prefix = {})
{
    return {QStringLiteral("org.quemusic.source.navidrome"),
            QStringLiteral("navidrome"), QStringLiteral("navidrome/admin"),
            QStringLiteral("admin"), QStringLiteral("Navidrome Admin"),
            {{QStringLiteral("serverUrl"),
              QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(prefix)},
             {QStringLiteral("username"), QStringLiteral("admin")},
             {QStringLiteral("quality"), QStringLiteral("original")}},
            std::move(secret)};
}

SourceConfigurationV2 configuration(quint16 port, QString prefix = {})
{
    return configuration(port, QByteArrayLiteral("test-password"), std::move(prefix));
}

SettingsFieldV2 field(const SettingsSchemaV2 &schema, const QString &id)
{
    for (const SettingsSectionV2 &section : schema)
        for (const SettingsFieldV2 &candidate : section.fields)
            if (candidate.id == id)
                return candidate;
    return {};
}

const QList<SourceActionV2> allSourceActions{
    SourceActionV2::Play, SourceActionV2::Artwork, SourceActionV2::Lyrics,
    SourceActionV2::Download, SourceActionV2::Favorite, SourceActionV2::Unfavorite,
    SourceActionV2::Rating, SourceActionV2::Scrobble,
    SourceActionV2::CreatePlaylist, SourceActionV2::UpdatePlaylist,
    SourceActionV2::DeletePlaylist, SourceActionV2::AddPlaylistTracks,
    SourceActionV2::RemovePlaylistTracks, SourceActionV2::FetchPlayQueue,
    SourceActionV2::SavePlayQueue, SourceActionV2::FetchBookmarks,
    SourceActionV2::CreateBookmark, SourceActionV2::DeleteBookmark};

class ImmediateReply final : public QNetworkReply {
public:
    ImmediateReply(const QNetworkRequest &request, QByteArray body, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setFinished(true);
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }
    void publishBeforeReturn() { emit readyRead(); emit finished(); }
    void abort() override { m_aborted = true; }
protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, qint64(m_body.size() - m_offset));
        if (count <= 0)
            return -1;
        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_aborted = false;
};

class ImmediateNetworkAccessManager final : public QNetworkAccessManager {
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        Q_UNUSED(operation)
        Q_UNUSED(outgoingData)
        auto *reply = new ImmediateReply(
            request, QJsonDocument(subsonicOk()).toJson(QJsonDocument::Compact), this);
        reply->publishBeforeReturn();
        return reply;
    }
};

void enqueueSuccessfulHandshake(FakeNavidromeServer &server,
                                QJsonObject user = userResponse())
{
    server.enqueue(subsonicOk());
    server.enqueue(extensionsResponse({QStringLiteral("lyrics"),
                                       QStringLiteral("songLyrics")}));
    server.enqueue(std::move(user));
}

} // namespace

class NavidromeSourceTest : public QObject {
    Q_OBJECT

private slots:
    void packageAndQtMetadataAdvertiseV2();
    void pluginAdvertisesV2AndGenericSettings();
    void pluginCreatesOnlyBaseV2Session();
    void descriptorMatchesAbsentV2Providers();
    void legacyAndNamedSecretsProduceEquivalentControlledTokens();
    void malformedOrMissingPasswordFailsBeforeNetwork();
    void transportPreservesBasePathAndUsesFreshSalt();
    void transportMapsTypedErrorsAndRedactsDetails();
    void authoritativeHttpStatusOverridesSubsonicBody();
    void synchronousCompletionAndCancellationAreSafe();
    void openNegotiatesExtensionsAndCurrentUserRoles();
    void synchronousOpenInterruption_data();
    void synchronousOpenInterruption();
    void extensionsRetainConservativeCapabilities_data();
    void extensionsRetainConservativeCapabilities();
    void roleMappingIsLiteralAndIndependent();
    void unsupportedOrMalformedRoleResponseKeepsReadSessionReady();
    void authenticationAndNetworkFailuresSetTypedStates();
    void reconnectClearsCapabilitiesAndCloseReleasesReplies();
    void cancelSuppressesTerminalSignal();
    void searchMapsSongsAlbumsAndArtists();
    void searchAcceptsEmptyResultObject();
    void browseMapsRootAndDirectoryResponses();
    void resolveStreamReturnsAuthenticatedStreamDto();
    void artworkReturnsAuthenticatedArtworkDto();
    void lyricsFetchesSongMetadataThenLyrics();
    void mapsSubsonicErrorsAndMalformedJson();
    void cancelsLyricsSecondStageWithoutTerminalSignal();
    void lyricsUsesCachedSearchMetadata();
};

void NavidromeSourceTest::packageAndQtMetadataAdvertiseV2()
{
    const QString packagePath = QFINDTESTDATA("../plugins/navidrome-source/manifest.json.in");
    const QString qtPath = QFINDTESTDATA("../plugins/navidrome-source/plugin.json");
    QVERIFY(!packagePath.isEmpty());
    QVERIFY(!qtPath.isEmpty());
    QFile packageFile(packagePath);
    QFile qtFile(qtPath);
    QVERIFY(packageFile.open(QIODevice::ReadOnly));
    QVERIFY(qtFile.open(QIODevice::ReadOnly));
    const QJsonObject package = QJsonDocument::fromJson(packageFile.readAll()).object();
    const QJsonObject interface = package.value(QStringLiteral("interfaces")).toArray().at(0).toObject();
    QCOMPARE(interface.value(QStringLiteral("id")).toString(),
             QStringLiteral("org.quemusic.MusicSourcePlugin/2.0"));
    QCOMPARE(interface.value(QStringLiteral("version")).toString(), QStringLiteral("2.0"));
    const QJsonObject requirements = package.value(QStringLiteral("runtimeRequirements")).toObject();
    QCOMPARE(requirements.value(QStringLiteral("sourceSdkAbi")).toInt(), 2);
    QCOMPARE(requirements.value(QStringLiteral("qtMajor")).toInt(), QT_VERSION_MAJOR);
    QVERIFY(!requirements.value(QStringLiteral("architecture")).toString().isEmpty());
    QVERIFY(!requirements.value(QStringLiteral("buildKey")).toString().isEmpty());
    const QJsonObject qt = QJsonDocument::fromJson(qtFile.readAll()).object();
    QCOMPARE(qt.value(QStringLiteral("sdkAbi")).toInt(), 2);
}

void NavidromeSourceTest::pluginAdvertisesV2AndGenericSettings()
{
    NavidromeSourcePlugin plugin;
    IMusicSourcePluginV2 *base = &plugin;
    QCOMPARE(base->descriptor().pluginPackageId,
             QStringLiteral("org.quemusic.source.navidrome"));
    QCOMPARE(base->descriptor().sourceId, QStringLiteral("navidrome"));
    QCOMPARE(base->descriptor().sdkAbi, 2);
    auto *settings = qobject_cast<IPluginSettingsProviderV2 *>(&plugin);
    QVERIFY(settings != nullptr);
    const SettingsSchemaV2 schema = settings->settingsSchema();
    QCOMPARE(schema.size(), 1);
    QCOMPARE(schema.constFirst().id, QStringLiteral("connection"));
    QCOMPARE(field(schema, QStringLiteral("serverUrl")).type, SettingsFieldTypeV2::Url);
    QVERIFY(field(schema, QStringLiteral("serverUrl")).required);
    QCOMPARE(field(schema, QStringLiteral("username")).type, SettingsFieldTypeV2::Text);
    QCOMPARE(field(schema, QStringLiteral("password")).type, SettingsFieldTypeV2::Secret);
    QVERIFY(field(schema, QStringLiteral("password")).required);
    QVERIFY(field(schema, QStringLiteral("password")).secret);
    const SettingsFieldV2 quality = field(schema, QStringLiteral("quality"));
    QCOMPARE(quality.type, SettingsFieldTypeV2::Choice);
    QCOMPARE(quality.choices, QVariantList({QStringLiteral("original"),
                                            QStringLiteral("transcoded")}));
    QCOMPARE(quality.defaultValue, QVariant(QStringLiteral("original")));
}

void NavidromeSourceTest::pluginCreatesOnlyBaseV2Session()
{
    NavidromeSourcePlugin plugin;
    IMusicSourceSessionV2 *session = plugin.createSession(configuration(8533), &plugin);
    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &plugin);
    QVERIFY(qobject_cast<IPageProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IPlaybackProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IFavoriteProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IRatingProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IScrobbleProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IPlaylistProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IDownloadProviderV2 *>(session) == nullptr);
}

void NavidromeSourceTest::descriptorMatchesAbsentV2Providers()
{
    NavidromeSourcePlugin plugin;
    const SourceDescriptorV2 descriptor = plugin.descriptor();
    IMusicSourceSessionV2 *session = plugin.createSession(configuration(8533), &plugin);
    QVERIFY(session != nullptr);
    QVERIFY(qobject_cast<IPlaybackProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IFavoriteProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IRatingProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IScrobbleProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IPlaylistProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IDownloadProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IPlayQueueProviderV2 *>(session) == nullptr);
    QVERIFY(qobject_cast<IBookmarkProviderV2 *>(session) == nullptr);
    QCOMPARE(descriptor.declaredActions.size(), 0);
    for (SourceActionV2 action : allSourceActions)
        QCOMPARE(descriptor.declaredActions.value(action).state, AvailabilityV2::Unsupported);
}

void NavidromeSourceTest::legacyAndNamedSecretsProduceEquivalentControlledTokens()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk());
    server.enqueue(subsonicOk());
    auto named = encodeSourceSecretsV2({{QStringLiteral("password"),
                                          QByteArrayLiteral("test-password")}});
    QVERIFY(named.has_value());
    QNetworkAccessManager network;
    const auto fixedSalt = [] { return QStringLiteral("0123456789abcdef"); };
    NavidromeApiClient legacy(configuration(server.serverPort()), &network, fixedSalt);
    NavidromeApiClient envelope(configuration(server.serverPort(), *named), &network, fixedSalt);
    QSignalSpy legacySuccess(&legacy, &NavidromeApiClient::succeeded);
    QSignalSpy envelopeSuccess(&envelope, &NavidromeApiClient::succeeded);
    legacy.get(QStringLiteral("legacy"), QStringLiteral("ping"));
    envelope.get(QStringLiteral("named"), QStringLiteral("ping"));
    QTRY_COMPARE(legacySuccess.count(), 1);
    QTRY_COMPARE(envelopeSuccess.count(), 1);
    QCOMPARE(server.requests().size(), 2);
    const QString expectedToken = QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("test-password0123456789abcdef"), QCryptographicHash::Md5).toHex());
    for (const CapturedRequest &request : server.requests()) {
        const QUrlQuery query(request.url);
        QCOMPARE(query.queryItemValue(QStringLiteral("u")), QStringLiteral("admin"));
        QCOMPARE(query.queryItemValue(QStringLiteral("t")), expectedToken);
        QCOMPARE(query.queryItemValue(QStringLiteral("s")), QStringLiteral("0123456789abcdef"));
        QCOMPARE(query.queryItemValue(QStringLiteral("v")), QStringLiteral("1.16.1"));
        QCOMPARE(query.queryItemValue(QStringLiteral("c")), QStringLiteral("QueMusic"));
        QCOMPARE(query.queryItemValue(QStringLiteral("f")), QStringLiteral("json"));
        QVERIFY(!request.url.query().contains(QStringLiteral("p=")));
    }
}

void NavidromeSourceTest::malformedOrMissingPasswordFailsBeforeNetwork()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    QNetworkAccessManager network;
    const QList<QByteArray> secrets{
        QByteArrayLiteral("QueMusic.SourceSecrets/99\n{}"),
        *encodeSourceSecretsV2({{QStringLiteral("apiKey"), QByteArrayLiteral("PRIVATE")}}),
        *encodeSourceSecretsV2({{QStringLiteral("password"), QByteArray()}}),
        QByteArray()};
    for (const QByteArray &secret : secrets) {
        NavidromeApiClient client(configuration(server.serverPort(), secret), &network);
        QSignalSpy failure(&client, &NavidromeApiClient::failed);
        const QUuid requestId = client.get(QStringLiteral("probe"), QStringLiteral("ping"));
        QVERIFY(!requestId.isNull());
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(failure.constFirst().at(0).toUuid(), requestId);
        const SourceErrorV2 error = qvariant_cast<SourceErrorV2>(failure.constFirst().at(1));
        QCOMPARE(error.kind, SourceErrorKindV2::Authentication);
        QCOMPARE(error.messageKey, QStringLiteral("source.authentication.invalidCredentials"));
        QVERIFY(!error.detail.contains(QStringLiteral("PRIVATE")));
        QVERIFY(!error.detail.contains(QStringLiteral("QueMusic.SourceSecrets")));
    }
    QCOMPARE(server.requests().size(), 0);
}

void NavidromeSourceTest::transportPreservesBasePathAndUsesFreshSalt()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk());
    server.enqueue(subsonicOk());
    QNetworkAccessManager network;
    NavidromeApiClient client(configuration(server.serverPort(), QStringLiteral("/music/")), &network);
    QSignalSpy success(&client, &NavidromeApiClient::succeeded);
    client.get(QStringLiteral("first"), QStringLiteral("ping"),
               QUrlQuery(QStringLiteral("id=42&p=leaked&t=leaked&s=leaked")));
    QTRY_COMPARE(success.count(), 1);
    client.get(QStringLiteral("second"), QStringLiteral("ping"));
    QTRY_COMPARE(success.count(), 2);
    QCOMPARE(server.requests().at(0).url.path(), QStringLiteral("/music/rest/ping.view"));
    QCOMPARE(QUrlQuery(server.requests().at(0).url).queryItemValue(QStringLiteral("id")),
             QStringLiteral("42"));
    QVERIFY(!QUrlQuery(server.requests().at(0).url).hasQueryItem(QStringLiteral("p")));
    QVERIFY(QUrlQuery(server.requests().at(0).url).queryItemValue(QStringLiteral("t"))
            != QStringLiteral("leaked"));
    QVERIFY(QUrlQuery(server.requests().at(0).url).queryItemValue(QStringLiteral("s"))
            != QStringLiteral("leaked"));
    const QString firstSalt = QUrlQuery(server.requests().at(0).url)
                                  .queryItemValue(QStringLiteral("s"));
    const QString secondSalt = QUrlQuery(server.requests().at(1).url)
                                   .queryItemValue(QStringLiteral("s"));
    QVERIFY(!firstSalt.isEmpty());
    QVERIFY(!secondSalt.isEmpty());
    QVERIFY(firstSalt != secondSalt);
}

void NavidromeSourceTest::transportMapsTypedErrorsAndRedactsDetails()
{
    struct Case { int httpStatus; QByteArray body; SourceErrorKindV2 kind; };
    const QList<Case> cases{
        {200, QJsonDocument(subsonicError(40, QStringLiteral("test-password admin cookie")))
                  .toJson(QJsonDocument::Compact), SourceErrorKindV2::Authentication},
        {200, QJsonDocument(subsonicError(50, QStringLiteral("test-password admin cookie")))
                  .toJson(QJsonDocument::Compact), SourceErrorKindV2::Authorization},
        {200, QJsonDocument(subsonicError(70, QStringLiteral("test-password admin cookie")))
                  .toJson(QJsonDocument::Compact), SourceErrorKindV2::NotFound},
        {404, QByteArrayLiteral("not found: ?u=admin&t=PRIVATE&s=PRIVATE"),
         SourceErrorKindV2::Unsupported}};
    for (const Case &testCase : cases) {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueueRaw(testCase.body, testCase.httpStatus);
        QNetworkAccessManager network;
        NavidromeApiClient client(configuration(server.serverPort()), &network);
        QSignalSpy failure(&client, &NavidromeApiClient::failed);
        client.get(QStringLiteral("authorizationProbe"), QStringLiteral("download"),
                   QUrlQuery(QStringLiteral("id=42")));
        QTRY_COMPARE(failure.count(), 1);
        const SourceErrorV2 error = qvariant_cast<SourceErrorV2>(failure.constFirst().at(1));
        QCOMPARE(error.kind, testCase.kind);
        QCOMPARE(error.httpStatus, std::optional<int>(testCase.httpStatus));
        for (const QString &forbidden : {QStringLiteral("admin"),
                                        QStringLiteral("test-password"),
                                        QStringLiteral("PRIVATE"),
                                        QStringLiteral("cookie"),
                                        QStringLiteral("id=42")})
            QVERIFY2(!error.detail.contains(forbidden), qPrintable(error.detail));
    }
}

void NavidromeSourceTest::authoritativeHttpStatusOverridesSubsonicBody()
{
    struct Case {
        int httpStatus;
        int subsonicCode;
        SourceErrorKindV2 expected;
    };
    const QList<Case> cases{
        {401, 50, SourceErrorKindV2::Authentication},
        {404, 50, SourceErrorKindV2::Unsupported},
        {405, 40, SourceErrorKindV2::Unsupported},
        {501, 70, SourceErrorKindV2::Unsupported}};
    for (const Case &testCase : cases) {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueue(subsonicError(testCase.subsonicCode,
                                     QStringLiteral("body must not override HTTP status")),
                       testCase.httpStatus);
        QNetworkAccessManager network;
        NavidromeApiClient client(configuration(server.serverPort()), &network);
        QSignalSpy failure(&client, &NavidromeApiClient::failed);
        client.get(QStringLiteral("status-precedence"), QStringLiteral("ping"));
        QTRY_COMPARE(failure.count(), 1);
        const SourceErrorV2 error = qvariant_cast<SourceErrorV2>(failure.constFirst().at(1));
        QCOMPARE(error.kind, testCase.expected);
        QCOMPARE(error.httpStatus, std::optional<int>(testCase.httpStatus));
    }
}

void NavidromeSourceTest::synchronousCompletionAndCancellationAreSafe()
{
    ImmediateNetworkAccessManager network;
    NavidromeApiClient completed(configuration(8533), &network);
    QSignalSpy succeeded(&completed, &NavidromeApiClient::succeeded);
    QSignalSpy failed(&completed, &NavidromeApiClient::failed);
    const QUuid completedId = completed.get(QStringLiteral("sync"), QStringLiteral("ping"));
    QVERIFY(!completedId.isNull());
    QTRY_COMPARE(succeeded.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), completedId);

    NavidromeApiClient cancelled(configuration(8533), &network);
    QSignalSpy cancelledSuccess(&cancelled, &NavidromeApiClient::succeeded);
    QSignalSpy cancelledFailure(&cancelled, &NavidromeApiClient::failed);
    const QUuid cancelledId = cancelled.get(QStringLiteral("sync-cancel"), QStringLiteral("ping"));
    cancelled.cancel(cancelledId);
    QTest::qWait(20);
    QCOMPARE(cancelledSuccess.count(), 0);
    QCOMPARE(cancelledFailure.count(), 0);
}

void NavidromeSourceTest::openNegotiatesExtensionsAndCurrentUserRoles()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    enqueueSuccessfulHandshake(server);
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort(), QStringLiteral("/prefix")),
                                   &network);
    QSignalSpy started(&session, &IMusicSourceSessionV2::requestStarted);
    QSignalSpy changed(&session, &IMusicSourceSessionV2::capabilitiesChanged);
    const QUuid requestId = session.open();
    QVERIFY(!requestId.isNull());
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.constFirst().at(0).toUuid(), requestId);
    QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
    QVERIFY(changed.count() >= 1);
    QCOMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().at(0).url.path(), QStringLiteral("/prefix/rest/ping.view"));
    QCOMPARE(server.requests().at(1).url.path(),
             QStringLiteral("/prefix/rest/getOpenSubsonicExtensions.view"));
    QCOMPARE(server.requests().at(2).url.path(), QStringLiteral("/prefix/rest/getUser.view"));
    QCOMPARE(QUrlQuery(server.requests().at(2).url).queryItemValue(QStringLiteral("username")),
             QStringLiteral("admin"));
    QVERIFY(!QUrlQuery(server.requests().at(2).url).hasQueryItem(QStringLiteral("p")));
    const CapabilitySetV2 capabilities = session.capabilities();
    QCOMPARE(capabilities.action(SourceActionV2::Lyrics).state, AvailabilityV2::Available);
    QCOMPARE(capabilities.serverAction(SourceActionV2::Play).state, AvailabilityV2::Available);
    QCOMPARE(capabilities.accountAction(SourceActionV2::Play).state, AvailabilityV2::Available);
}

void NavidromeSourceTest::synchronousOpenInterruption_data()
{
    QTest::addColumn<int>("trigger");
    QTest::addColumn<bool>("cancelInsteadOfClose");

    constexpr int RequestStarted = 0;
    constexpr int StateChanged = 1;
    constexpr int CapabilitiesChanged = 2;
    QTest::newRow("cancel-from-requestStarted") << RequestStarted << true;
    QTest::newRow("close-from-requestStarted") << RequestStarted << false;
    QTest::newRow("cancel-from-stateChanged") << StateChanged << true;
    QTest::newRow("close-from-stateChanged") << StateChanged << false;
    QTest::newRow("cancel-from-capabilitiesChanged") << CapabilitiesChanged << true;
    QTest::newRow("close-from-capabilitiesChanged") << CapabilitiesChanged << false;
}

void NavidromeSourceTest::synchronousOpenInterruption()
{
    QFETCH(int, trigger);
    QFETCH(bool, cancelInsteadOfClose);
    constexpr int RequestStarted = 0;
    constexpr int StateChanged = 1;
    constexpr int CapabilitiesChanged = 2;

    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    enqueueSuccessfulHandshake(server);
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QUuid startedId;
    bool interruptionHadRequestId = false;
    bool interrupted = false;
    bool sawReady = false;
    const auto interrupt = [&] {
        if (interrupted)
            return;
        interrupted = true;
        interruptionHadRequestId = !startedId.isNull();
        if (cancelInsteadOfClose)
            session.cancel(startedId);
        else
            session.close();
    };
    connect(&session, &IMusicSourceSessionV2::requestStarted, &session,
            [&](const QUuid &requestId) {
        startedId = requestId;
        if (trigger == RequestStarted)
            interrupt();
    });
    connect(&session, &IMusicSourceSessionV2::stateChanged, &session,
            [&](SourceSessionStateV2 state) {
        sawReady = sawReady || state == SourceSessionStateV2::Ready;
        if (trigger == StateChanged && state == SourceSessionStateV2::Connecting)
            interrupt();
    });
    connect(&session, &IMusicSourceSessionV2::capabilitiesChanged, &session,
            [&](const CapabilitySetV2 &capabilities) {
        if (trigger == CapabilitiesChanged && !capabilities.serverActions.isEmpty())
            interrupt();
    });

    const QUuid returnedId = session.open();
    if (trigger == CapabilitiesChanged)
        QTRY_VERIFY_WITH_TIMEOUT(interrupted, 1000);
    else
        QVERIFY(interrupted);
    QVERIFY(interruptionHadRequestId);
    QVERIFY(!returnedId.isNull());
    QCOMPARE(returnedId, startedId);
    QTest::qWait(150);
    QCOMPARE(session.state(), SourceSessionStateV2::Closed);
    QVERIFY(!sawReady);
    QVERIFY(session.capabilities().serverActions.isEmpty());
    QVERIFY(session.capabilities().accountActions.isEmpty());
    QCOMPARE(server.requests().size(), trigger == CapabilitiesChanged ? 1 : 0);
}

void NavidromeSourceTest::extensionsRetainConservativeCapabilities_data()
{
    QTest::addColumn<QByteArray>("extensionBody");
    QTest::addColumn<int>("httpStatus");
    QTest::newRow("empty-extension-set")
        << QJsonDocument(extensionsResponse({})).toJson(QJsonDocument::Compact) << 200;
    QTest::newRow("recognized-extension-set")
        << QJsonDocument(extensionsResponse({QStringLiteral("lyrics"),
                                             QStringLiteral("songLyrics")}))
               .toJson(QJsonDocument::Compact)
        << 200;
    QTest::newRow("malformed-extension-response") << QByteArrayLiteral("not-json") << 200;
    QTest::newRow("unsupported-extension-endpoint") << QByteArrayLiteral("not-found") << 404;
}

void NavidromeSourceTest::extensionsRetainConservativeCapabilities()
{
    QFETCH(QByteArray, extensionBody);
    QFETCH(int, httpStatus);
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk());
    server.enqueueRaw(extensionBody, httpStatus);
    server.enqueue(userResponse());
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    session.open();
    QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
    QCOMPARE(server.requests().size(), 3);
    const CapabilitySetV2 capabilities = session.capabilities();
    QCOMPARE(capabilities.serverActions.size(), allSourceActions.size());
    for (SourceActionV2 action : allSourceActions)
        QCOMPARE(capabilities.serverAction(action).state, AvailabilityV2::Available);
}

void NavidromeSourceTest::roleMappingIsLiteralAndIndependent()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    enqueueSuccessfulHandshake(server, userResponse(QStringLiteral("admin"), false, false,
                                                    false, false));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    session.open();
    QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
    const CapabilitySetV2 caps = session.capabilities();
    for (SourceActionV2 action : {SourceActionV2::Play, SourceActionV2::Artwork,
                                  SourceActionV2::Download, SourceActionV2::CreatePlaylist,
                                  SourceActionV2::UpdatePlaylist, SourceActionV2::DeletePlaylist,
                                  SourceActionV2::AddPlaylistTracks,
                                  SourceActionV2::RemovePlaylistTracks}) {
        QCOMPARE(caps.serverAction(action).state, AvailabilityV2::Available);
        QCOMPARE(caps.accountAction(action).state, AvailabilityV2::Unavailable);
        QCOMPARE(caps.action(action).state, AvailabilityV2::Unavailable);
    }
    QCOMPARE(caps.accountAction(SourceActionV2::Rating).state, AvailabilityV2::Available);
    QCOMPARE(caps.accountAction(SourceActionV2::Scrobble).state, AvailabilityV2::Available);
    QCOMPARE(caps.accountAction(SourceActionV2::Lyrics).state, AvailabilityV2::Available);
}

void NavidromeSourceTest::unsupportedOrMalformedRoleResponseKeepsReadSessionReady()
{
    {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueue(subsonicOk());
        server.enqueueRaw(QByteArrayLiteral("missing"), 404);
        server.enqueue(userResponse());
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()), &network);
        session.open();
        QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QCOMPARE(session.capabilities().serverAction(SourceActionV2::Play).state,
                 AvailabilityV2::Available);
        QCOMPARE(session.capabilities().accountAction(SourceActionV2::Play).state,
                 AvailabilityV2::Available);
    }
    {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueue(subsonicOk());
        server.enqueue(extensionsResponse({QStringLiteral("songLyrics")}));
        server.enqueueRaw(QByteArrayLiteral("missing"), 404);
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()), &network);
        session.open();
        QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QCOMPARE(session.capabilities().accountAction(SourceActionV2::Lyrics).state,
                 AvailabilityV2::Unavailable);
    }
    {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        QJsonObject malformedUser = userResponse();
        QJsonObject response = malformedUser.value(QStringLiteral("subsonic-response")).toObject();
        QJsonObject user = response.value(QStringLiteral("user")).toObject();
        user.remove(QStringLiteral("streamRole"));
        user.insert(QStringLiteral("coverArtRole"), QStringLiteral("true"));
        response.insert(QStringLiteral("user"), user);
        malformedUser.insert(QStringLiteral("subsonic-response"), response);
        enqueueSuccessfulHandshake(server, malformedUser);
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()), &network);
        session.open();
        QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QCOMPARE(session.capabilities().accountAction(SourceActionV2::Play).state,
                 AvailabilityV2::Unavailable);
        QCOMPARE(session.capabilities().accountAction(SourceActionV2::Artwork).state,
                 AvailabilityV2::Unavailable);
        QCOMPARE(session.capabilities().accountAction(SourceActionV2::Download).state,
                 AvailabilityV2::Available);
        QCOMPARE(session.capabilities().accountAction(SourceActionV2::Rating).state,
                 AvailabilityV2::Available);
    }
}

void NavidromeSourceTest::authenticationAndNetworkFailuresSetTypedStates()
{
    {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueue(subsonicError(40, QStringLiteral("Wrong password")));
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()), &network);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        const QUuid id = session.open();
        QTRY_COMPARE(session.state(), SourceSessionStateV2::AuthenticationRequired);
        QCOMPARE(failure.count(), 1);
        QCOMPARE(failure.constFirst().at(0).toUuid(), id);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failure.constFirst().at(1)).kind,
                 SourceErrorKindV2::Authentication);
    }
    {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueue(subsonicOk());
        server.enqueue(extensionsResponse({QStringLiteral("songLyrics")}));
        server.enqueue(subsonicError(40, QStringLiteral("Wrong password")));
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()), &network);
        session.open();
        QTRY_COMPARE(session.state(), SourceSessionStateV2::AuthenticationRequired);
    }
    {
        FakeNavidromeServer server;
        QVERIFY2(server.start(), qPrintable(server.errorString()));
        server.enqueueRaw(QByteArrayLiteral("temporarily unavailable"), 503);
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()), &network);
        session.open();
        QTRY_COMPARE(session.state(), SourceSessionStateV2::Failed);
    }
}

void NavidromeSourceTest::reconnectClearsCapabilitiesAndCloseReleasesReplies()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    enqueueSuccessfulHandshake(server);
    server.enqueueHeld();
    QNetworkAccessManager network;
    auto *session = new NavidromeSourceSession(configuration(server.serverPort()), &network);
    session->open();
    QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
    QCOMPARE(session->capabilities().action(SourceActionV2::Play).state,
             AvailabilityV2::Available);
    const QUuid reconnectId = session->open();
    QCOMPARE(session->state(), SourceSessionStateV2::Connecting);
    QVERIFY(session->capabilities().serverActions.isEmpty());
    QVERIFY(session->capabilities().accountActions.isEmpty());
    session->cancel(reconnectId);
    QCOMPARE(session->state(), SourceSessionStateV2::Closed);
    delete session;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(network.findChildren<QNetworkReply *>().isEmpty());
}

void NavidromeSourceTest::cancelSuppressesTerminalSignal()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueueHeld();
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    QSignalSpy failed(&session, &NavidromeSourceSession::legacyRequestFailed);
    const QUuid requestId = session.ping();
    QVERIFY(!requestId.isNull());
    session.cancel(requestId);
    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void NavidromeSourceTest::searchMapsSongsAlbumsAndArtists()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk({{QStringLiteral("searchResult3"),
                                QJsonObject{{QStringLiteral("song"),
                                             QJsonArray{QJsonObject{
                                                 {QStringLiteral("id"), QStringLiteral("song-1")},
                                                 {QStringLiteral("title"), QStringLiteral("Song")},
                                                 {QStringLiteral("artist"), QStringLiteral("Artist")},
                                                 {QStringLiteral("album"), QStringLiteral("Album")},
                                                 {QStringLiteral("duration"), 12},
                                                 {QStringLiteral("coverArt"), QStringLiteral("cover-1")}}}},
                                            {QStringLiteral("album"),
                                             QJsonArray{QJsonObject{
                                                 {QStringLiteral("id"), QStringLiteral("album-1")},
                                                 {QStringLiteral("name"), QStringLiteral("Album")},
                                                 {QStringLiteral("artist"), QStringLiteral("Artist")}}}},
                                            {QStringLiteral("artist"),
                                             QJsonArray{QJsonObject{
                                                 {QStringLiteral("id"), QStringLiteral("artist-1")},
                                                 {QStringLiteral("name"), QStringLiteral("Artist")}}}}}}}));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    const QUuid requestId = session.search({QStringLiteral("Song"), 10});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("search"));
    const QJsonArray items = succeeded.constFirst().at(2).value<QJsonValue>()
                                 .toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("song-1"));
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("coverArtId")).toString(),
             QStringLiteral("cover-1"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("album"));
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("artist"));
}

void NavidromeSourceTest::searchAcceptsEmptyResultObject()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk({{QStringLiteral("searchResult3"), QJsonObject{}}}));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    QSignalSpy failed(&session, &NavidromeSourceSession::legacyRequestFailed);
    session.search({QStringLiteral("unmatched query"), 10});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(failed.count(), 0);
    QVERIFY(succeeded.constFirst().at(2).value<QJsonValue>().toObject()
                .value(QStringLiteral("items")).toArray().isEmpty());
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
                ? QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"indexes\":{\"artist\":[{\"id\":\"artist-1\",\"name\":\"Artist One\"}]}}}")
                : QByteArray("{\"subsonic-response\":{\"status\":\"ok\",\"directory\":{\"id\":\"album-1\",\"name\":\"Album\",\"child\":[{\"id\":\"song-1\",\"title\":\"Song\",\"artist\":\"Artist\"}]}}}");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(response.size()) +
                          "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);

    session.browse({});
    QVERIFY(succeeded.wait(1000));
    const QJsonArray rootItems = succeeded.constFirst().at(2).value<QJsonValue>()
                                     .toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(rootItems.size(), 1);
    QCOMPARE(rootItems.at(0).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("artist"));
    QCOMPARE(rootItems.at(0).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Artist One"));
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
    NavidromeSourceSession session(configuration(8533), &network,
                                   [] { return QStringLiteral("stream-salt"); });
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    const QUuid requestId = session.resolveStream(
        {QStringLiteral("navidrome"), QStringLiteral("song-1")});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    const QJsonObject result = succeeded.constFirst().at(2).value<QJsonValue>().toObject();
    QCOMPARE(result.value(QStringLiteral("track")).toObject()
                 .value(QStringLiteral("nativeId")).toString(), QStringLiteral("song-1"));
    const QUrl url(result.value(QStringLiteral("url")).toString());
    QCOMPARE(url.path(), QStringLiteral("/rest/stream.view"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("id")), QStringLiteral("song-1"));
    QVERIFY(!url.toString().contains(QStringLiteral("test-password")));
    QVERIFY(result.value(QStringLiteral("seekable")).toBool());
}

void NavidromeSourceTest::artworkReturnsAuthenticatedArtworkDto()
{
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(8533), &network,
                                   [] { return QStringLiteral("art-salt"); });
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    session.fetchArtwork({QStringLiteral("navidrome"), QStringLiteral("cover-1")});
    QVERIFY(succeeded.wait(1000));
    const QUrl url(succeeded.constFirst().at(2).value<QJsonValue>().toObject()
                       .value(QStringLiteral("url")).toString());
    QCOMPARE(url.path(), QStringLiteral("/rest/getCoverArt.view"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("id")), QStringLiteral("cover-1"));
    QVERIFY(!url.toString().contains(QStringLiteral("test-password")));
}

void NavidromeSourceTest::lyricsFetchesSongMetadataThenLyrics()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk({{QStringLiteral("song"),
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("song-1")},
                                            {QStringLiteral("artist"), QStringLiteral("Artist")},
                                            {QStringLiteral("title"), QStringLiteral("Song")}}}}));
    server.enqueue(subsonicOk({{QStringLiteral("lyrics"),
                                QJsonObject{{QStringLiteral("value"), QStringLiteral("line one")},
                                            {QStringLiteral("synced"), false}}}}));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    const QUuid requestId = session.fetchLyrics(
        {QStringLiteral("navidrome"), QStringLiteral("song-1")});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.count(), 1);
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    const QJsonObject result = succeeded.constFirst().at(2).value<QJsonValue>().toObject();
    QCOMPARE(result.value(QStringLiteral("lyrics")).toString(), QStringLiteral("line one"));
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(0).url.path(), QStringLiteral("/rest/getSong.view"));
    QCOMPARE(server.requests().at(1).url.path(), QStringLiteral("/rest/getLyrics.view"));
    QCOMPARE(QUrlQuery(server.requests().at(1).url).queryItemValue(QStringLiteral("artist")),
             QStringLiteral("Artist"));
    QCOMPARE(QUrlQuery(server.requests().at(1).url).queryItemValue(QStringLiteral("title")),
             QStringLiteral("Song"));
}

void NavidromeSourceTest::mapsSubsonicErrorsAndMalformedJson()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicError(40, QStringLiteral("Wrong credentials")));
    server.enqueueRaw(QByteArrayLiteral("not-json"));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy failed(&session, &NavidromeSourceSession::legacyRequestFailed);
    session.ping();
    QVERIFY(failed.wait(1000));
    QCOMPARE(qvariant_cast<SourceErrorV2>(failed.constFirst().at(1)).kind,
             SourceErrorKindV2::Authentication);
    session.search({QStringLiteral("Song"), 1});
    QVERIFY(failed.wait(1000));
    QCOMPARE(qvariant_cast<SourceErrorV2>(failed.constLast().at(1)).kind,
             SourceErrorKindV2::InvalidRequest);
}

void NavidromeSourceTest::cancelsLyricsSecondStageWithoutTerminalSignal()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    server.enqueue(subsonicOk({{QStringLiteral("song"),
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("song-1")},
                                            {QStringLiteral("artist"), QStringLiteral("Artist")},
                                            {QStringLiteral("title"), QStringLiteral("Song")}}}}));
    server.enqueueHeld();
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    QSignalSpy failed(&session, &NavidromeSourceSession::legacyRequestFailed);
    const QUuid requestId = session.fetchLyrics(
        {QStringLiteral("navidrome"), QStringLiteral("song-1")});
    QTRY_COMPARE(server.requests().size(), 2);
    session.cancel(requestId);
    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void NavidromeSourceTest::lyricsUsesCachedSearchMetadata()
{
    FakeNavidromeServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    const QJsonObject cachedSong{{QStringLiteral("id"), QStringLiteral("song-1")},
                                 {QStringLiteral("title"), QStringLiteral("Song")},
                                 {QStringLiteral("artist"), QStringLiteral("Artist")}};
    server.enqueue(subsonicOk({{QStringLiteral("searchResult3"),
                                QJsonObject{{QStringLiteral("song"),
                                             QJsonArray{cachedSong}}}}}));
    server.enqueue(subsonicOk({{QStringLiteral("lyrics"),
                                QJsonObject{{QStringLiteral("value"), QStringLiteral("line one")},
                                            {QStringLiteral("synced"), false}}}}));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy succeeded(&session, &NavidromeSourceSession::legacyRequestSucceeded);
    session.search({QStringLiteral("Song"), 1});
    QVERIFY(succeeded.wait(1000));
    succeeded.clear();
    session.fetchLyrics({QStringLiteral("navidrome"), QStringLiteral("song-1")});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(1).url.path(), QStringLiteral("/rest/getLyrics.view"));
}

QTEST_MAIN(NavidromeSourceTest)
#include "tst_NavidromeSource.moc"
