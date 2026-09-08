#include "NavidromeApiClient.h"
#include "NavidromeMappers.h"
#include "NavidromeSourcePlugin.h"
#include "NavidromeSourceSession.h"
#include "PageCache.h"
#include "v2/ISourceProvidersV2.h"
#include "v2/SourceSecretsV2.h"

#include <QCryptographicHash>
#include <QDir>
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
#include <QTemporaryDir>
#include <QUrlQuery>

#include <cstring>
#include <memory>
#include <utility>

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
        QByteArray contentType = "application/json";
        qint64 declaredLength = -1;
        bool sendContentLength = true;
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
                    if (m_responses.isEmpty())
                        return;
                    const Response response = m_responses.takeFirst();
                    if (!response.respond) {
                        m_held.append({socket,response});
                        return;
                    }
                    sendResponse(socket,response);
                });
            }
        });
    }

    bool start() { return listen(QHostAddress::LocalHost); }
    void enqueue(const QJsonObject &response, int status = 200)
    {
        m_responses.append({status, QJsonDocument(response).toJson(QJsonDocument::Compact),
                            {}, true, "application/json", -1});
    }
    void enqueueRaw(QByteArray body, int status = 200)
    {
        m_responses.append({status, std::move(body), {}, true, "application/json", -1});
    }
    void enqueueBytes(QByteArray body, QByteArray contentType = "application/octet-stream",
                      qint64 declaredLength = -1, int status = 200)
    {
        m_responses.append({status, std::move(body), {}, true, std::move(contentType),
                            declaredLength});
    }
    void enqueueBytesWithoutLength(QByteArray body,
                                   QByteArray contentType = "application/octet-stream")
    {
        m_responses.append({200,std::move(body),{},true,std::move(contentType),-1,false});
    }
    void enqueueHeld(QByteArray body = {},QByteArray contentType = "application/octet-stream")
    {
        m_responses.append({200,std::move(body),{},false,std::move(contentType),-1,true});
    }
    void releaseHeld()
    {
        const auto held=std::exchange(m_held,{});
        for (const auto &[socket,response]:held)
            if (socket) sendResponse(socket,response);
    }
    const QList<CapturedRequest> &requests() const { return m_requests; }

private:
    void sendResponse(QTcpSocket *socket,const Response &response)
    {
        const QByteArray statusText=response.status==200 ? "OK" : "Error";
        QByteArray wire="HTTP/1.1 "+QByteArray::number(response.status)+" "+statusText
            +"\r\nContent-Type: "+response.contentType+"\r\n";
        for (const auto &header:response.headers)
            wire+=header.first+": "+header.second+"\r\n";
        if (response.sendContentLength)
            wire+="Content-Length: "+QByteArray::number(response.declaredLength>=0
                                                            ? response.declaredLength
                                                            : response.body.size())+"\r\n";
        wire+="Connection: close\r\n\r\n"+response.body;
        socket->write(wire);
        socket->disconnectFromHost();
    }
    QList<Response> m_responses;
    QList<CapturedRequest> m_requests;
    QList<QPair<QPointer<QTcpSocket>,Response>> m_held;
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
const QList<SourceActionV2> implementedSourceActions{
    SourceActionV2::Play, SourceActionV2::Artwork, SourceActionV2::Lyrics,
    SourceActionV2::Download, SourceActionV2::Favorite, SourceActionV2::Unfavorite,
    SourceActionV2::Rating, SourceActionV2::Scrobble, SourceActionV2::CreatePlaylist, SourceActionV2::UpdatePlaylist,
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

struct ReadObservation {
    QList<qint64> requestedSizes;
    qint64 bytesRead = 0;
    qint64 configuredReadBufferSize = -1;
};

class SingleAvailableChunkReply final : public QNetworkReply {
public:
    SingleAvailableChunkReply(const QNetworkRequest &request,QByteArray body,
                              QString mimeType,ReadObservation *observation,QObject *parent)
        : QNetworkReply(parent),m_body(std::move(body)),m_observation(observation)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);
        setHeader(QNetworkRequest::ContentTypeHeader,std::move(mimeType));
        open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        setFinished(true);
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size()-m_offset+QNetworkReply::bytesAvailable();
    }
    void abort() override {}
protected:
    qint64 readData(char *data,qint64 maxSize) override
    {
        m_observation->requestedSizes.append(maxSize);
        m_observation->configuredReadBufferSize=readBufferSize();
        const qint64 count=qMin(maxSize,qint64(m_body.size()-m_offset));
        if (count<=0) return -1;
        memcpy(data,m_body.constData()+m_offset,size_t(count));
        m_offset+=count;
        m_observation->bytesRead+=count;
        return count;
    }
private:
    QByteArray m_body;
    ReadObservation *m_observation;
    qint64 m_offset=0;
};

class SingleAvailableChunkNetwork final : public QNetworkAccessManager {
public:
    SingleAvailableChunkNetwork(QByteArray body,QString mimeType,
                                ReadObservation *observation,QObject *parent=nullptr)
        : QNetworkAccessManager(parent),m_body(std::move(body)),m_mimeType(std::move(mimeType)),
          m_observation(observation)
    {
    }
protected:
    QNetworkReply *createRequest(Operation operation,const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        Q_UNUSED(operation)
        Q_UNUSED(outgoingData)
        return new SingleAvailableChunkReply(request,m_body,m_mimeType,m_observation,this);
    }
private:
    QByteArray m_body;
    QString m_mimeType;
    ReadObservation *m_observation;
};

QVariant mediaRefDto(const MediaRefV2 &ref)
{
    return QVariantMap{{QStringLiteral("sourcePluginId"),ref.sourcePluginId},
                       {QStringLiteral("sourceInstanceId"),ref.sourceInstanceId},
                       {QStringLiteral("accountId"),ref.accountId},
                       {QStringLiteral("entityType"),int(ref.entityType)},
                       {QStringLiteral("entityId"),ref.entityId}};
}

QVariant pageDto(const PageResultV2 &page)
{
    QVariantList sections;
    for (const auto &section:page.sections) {
        QVariantList items;
        for (const auto &item:section.items) {
            QVariantMap actions;
            for (auto action=item.availableActions.cbegin();action!=item.availableActions.cend();++action)
                actions.insert(QString::number(int(action.key())),
                               QVariantMap{{QStringLiteral("state"),int(action->state)},
                                           {QStringLiteral("reasonKey"),action->reasonKey},
                                           {QStringLiteral("constraints"),action->constraints}});
            items.append(QVariantMap{{QStringLiteral("ref"),mediaRefDto(item.ref)},
                                     {QStringLiteral("title"),item.title},
                                     {QStringLiteral("subtitle"),item.subtitle},
                                     {QStringLiteral("artists"),item.artists},
                                     {QStringLiteral("album"),item.album},
                                     {QStringLiteral("durationMs"),item.durationMs},
                                     {QStringLiteral("artworkId"),item.artworkId},
                                     {QStringLiteral("externalIds"),item.externalIds},
                                     {QStringLiteral("metadata"),item.metadata},
                                     {QStringLiteral("availableActions"),actions}});
        }
        sections.append(QVariantMap{{QStringLiteral("sectionId"),section.sectionId},
                                    {QStringLiteral("titleKey"),section.titleKey},
                                    {QStringLiteral("kind"),int(section.kind)},
                                    {QStringLiteral("layoutHint"),section.layoutHint},
                                    {QStringLiteral("items"),items},
                                    {QStringLiteral("nextCursor"),section.nextCursor},
                                    {QStringLiteral("hasMore"),section.hasMore}});
    }
    QVariantMap states;
    for (auto state=page.sourceStates.cbegin();state!=page.sourceStates.cend();++state) {
        QVariantMap value{{QStringLiteral("state"),int(state->state)}};
        if (state->error)
            value.insert(QStringLiteral("error"),
                         QVariantMap{{QStringLiteral("kind"),int(state->error->kind)},
                                     {QStringLiteral("messageKey"),state->error->messageKey},
                                     {QStringLiteral("detail"),state->error->detail},
                                     {QStringLiteral("httpStatus"),state->error->httpStatus
                                          ? QVariant(*state->error->httpStatus):QVariant()},
                                     {QStringLiteral("retryable"),state->error->retryable}});
        states.insert(state.key(),value);
    }
    return QVariantMap{{QStringLiteral("sections"),sections},
                       {QStringLiteral("sourceStates"),states},
                       {QStringLiteral("cached"),page.cached},
                       {QStringLiteral("complete"),page.complete}};
}

bool variantContains(const QVariant &value,const QList<QByteArray> &needles)
{
    const auto containsNeedle=[&needles](const QByteArray &bytes) {
        for (const auto &needle:needles)
            if (bytes.contains(needle)) return true;
        return false;
    };
    if (value.metaType().id()==QMetaType::QVariantMap) {
        const QVariantMap map=value.toMap();
        for (auto it=map.cbegin();it!=map.cend();++it)
            if (containsNeedle(it.key().toUtf8()) || variantContains(it.value(),needles)) return true;
        return false;
    }
    if (value.metaType().id()==QMetaType::QVariantList) {
        for (const auto &item:value.toList())
            if (variantContains(item,needles)) return true;
        return false;
    }
    if (value.metaType().id()==QMetaType::QStringList) {
        for (const auto &item:value.toStringList())
            if (containsNeedle(item.toUtf8())) return true;
        return false;
    }
    if (value.metaType().id()==QMetaType::QByteArray) return containsNeedle(value.toByteArray());
    if (value.canConvert<QUrl>()) return containsNeedle(value.toUrl().toEncoded());
    if (value.canConvert<QString>()) return containsNeedle(value.toString().toUtf8());
    return false;
}

void enqueueSuccessfulHandshake(FakeNavidromeServer &server,
                                QJsonObject user = userResponse())
{
    server.enqueue(subsonicOk());
    server.enqueue(extensionsResponse({QStringLiteral("lyrics"),
                                       QStringLiteral("songLyrics")}));
    server.enqueue(std::move(user));
}

MediaRefV2 media(QString id = QStringLiteral("42"),
                 MediaEntityTypeV2 type = MediaEntityTypeV2::Track)
{
    return {QStringLiteral("navidrome"),
            QStringLiteral("navidrome/admin"), QStringLiteral("admin"),
            type, std::move(id)};
}

PageQueryV2 pageQuery(PageSectionKindV2 section)
{
    PageQueryV2 query;
    query.section = section;
    query.scope.sourceInstanceId = QStringLiteral("navidrome/admin");
    query.limit = 2;
    return query;
}

} // namespace

class NavidromeSourceTest : public QObject {
    Q_OBJECT

private slots:
    void task12ScrobbleMappedTrack();
    void task12Scrobble();
    void descriptorIdentityRoutesMappedMedia();
    void task11ProvidersAndWireContracts();
    void task11Actions_data();
    void task11Actions();
    void task11ValidationAndLifecycle();
    void task11PermissionDowngrade();
    void task11PlaylistsPage();
    void task11RejectsMalformedPlaylists();
    void packageAndQtMetadataAdvertiseV2();
    void pluginAdvertisesV2AndGenericSettings();
    void pluginCreatesImplementedV2ProvidersOnly();
    void descriptorMatchesImplementedV2Providers();
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
    void pageEndpoint_data();
    void pageEndpoint();
    void drillDownUsesTypedFilterEndpoint_data();
    void drillDownUsesTypedFilterEndpoint();
    void favoritesMapsSongsAlbumsAndArtists();
    void favoritesContinuationIsSectionSpecific();
    void searchContinuationKeepsCategoryOffsetsIndependent();
    void playlistTracksCarryAbsoluteOccurrenceMetadata();
    void streamDescriptorUsesAuthenticatedUrlWithoutPersistingIt();
    void artworkResolvesStableServerCoverIdsAndAdvertisesOnlySupportedEntities();
    void artworkPayloadIsBoundedAndTyped();
    void artworkBoundaryAcceptsExactAndRejectsAccumulatedOverflow();
    void successfulMediaReadsAreBounded_data();
    void successfulMediaReadsAreBounded();
    void lyricsUsesNegotiatedEndpoint();
    void downloadRejectsInvalidDestinationsBeforeNetwork();
    void downloadRejectsDanglingSymlinkBeforeNetwork();
    void downloadWritesRequestedLocalFileAtomically();
    void downloadRenameRaceAndCancellationLeaveNoPartialFiles();
    void binaryMediaRejectsSubsonicErrorBodiesAndCleansUp();
    void mediaResponsePrecedence_data();
    void mediaResponsePrecedence();
    void downloadWriteFailureRemovesArtifacts();
    void downloadFinalFlushFailureRemovesArtifacts();
    void downloadNetworkFailureRemovesArtifacts();
    void task10RequestStartedReentrancy_data();
    void task10RequestStartedReentrancy();
    void cancelledPageDropsLateServerResponse();
    void artworkSecondStageTeardownDropsLateResponse_data();
    void artworkSecondStageTeardownDropsLateResponse();
    void closeAndDestructionCleanDownloadTemporaryFiles();
    void pageDtoDoesNotLeakAuthenticatedRequestData();
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

void NavidromeSourceTest::pluginCreatesImplementedV2ProvidersOnly()
{
    NavidromeSourcePlugin plugin;
    IMusicSourceSessionV2 *session = plugin.createSession(configuration(8533), &plugin);
    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &plugin);
    // Missing Q_INTERFACES declarations makes implemented providers invisible to the host.
    QVERIFY(qobject_cast<IPageProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IPlaybackProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IFavoriteProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IRatingProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IScrobbleProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IPlaylistProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IDownloadProviderV2 *>(session) != nullptr);
}

void NavidromeSourceTest::descriptorIdentityRoutesMappedMedia()
{
    NavidromeSourcePlugin plugin;
    const auto descriptor = plugin.descriptor();
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(8533), &network);
    QCOMPARE(session.identity().sourcePluginId, descriptor.sourceId);
    QVERIFY(descriptor.sourceId != descriptor.pluginPackageId);
    const SourceIdentityV2 owner{descriptor.sourceId, QStringLiteral("navidrome/admin"),
                                 QStringLiteral("admin"), QStringLiteral("Admin")};
    const auto mapped = NavidromeMappers::song(
        QJsonObject{{"id", "42"}, {"title", "Track"}}, owner);
    auto *provider = qobject_cast<IPlaybackProviderV2 *>(&session);
    QVERIFY(provider);
    QSignalSpy streams(&session, &IMusicSourceSessionV2::streamReady);
    QSignalSpy failures(&session, &IMusicSourceSessionV2::requestFailed);
    const auto accepted = provider->resolveStream(mapped.ref);
    QTRY_COMPARE(streams.size(), 1);
    QCOMPARE(streams[0][0].toUuid(), accepted);
    QCOMPARE(qvariant_cast<StreamDescriptorV2>(streams[0][1]).media, mapped.ref);
    QCOMPARE(failures.size(), 0);
    for (const QString &wrongSource : {descriptor.pluginPackageId, QStringLiteral("foreign")}) {
        auto rejected = mapped.ref;
        rejected.sourcePluginId = wrongSource;
        const auto request = provider->resolveStream(rejected);
        QTRY_COMPARE(failures.size(), 1);
        const auto failure = failures.takeFirst();
        QCOMPARE(failure[0].toUuid(), request);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failure[1]).kind, SourceErrorKindV2::InvalidRequest);
        const auto artworkRequest = session.fetchArtwork(rejected);
        QTRY_COMPARE(failures.size(), 1);
        const auto artworkFailure = failures.takeFirst();
        QCOMPARE(artworkFailure[0].toUuid(), artworkRequest);
        QCOMPARE(qvariant_cast<SourceErrorV2>(artworkFailure[1]).kind, SourceErrorKindV2::InvalidRequest);
    }
    QCOMPARE(streams.size(), 1);
}

void NavidromeSourceTest::task12ScrobbleMappedTrack()
{
    const SourceIdentityV2 owner{"navidrome","navidrome/admin","admin","Admin"};
    const QJsonObject value{{"id","42"},{"title","Track"},{"name","Name"}};
    const auto song=NavidromeMappers::song(value,owner);
    QCOMPARE(song.ref.entityType,MediaEntityTypeV2::Track);
    QCOMPARE(song.availableActions.value(SourceActionV2::Scrobble).state,AvailabilityV2::Available);
    QCOMPARE(NavidromeMappers::album(value,owner).availableActions.value(SourceActionV2::Scrobble).state,
             AvailabilityV2::Unsupported);
    QCOMPARE(NavidromeMappers::artist(value,owner).availableActions.value(SourceActionV2::Scrobble).state,
             AvailabilityV2::Unsupported);
}

void NavidromeSourceTest::task12Scrobble()
{
    FakeNavidromeServer server; QVERIFY(server.start()); QNetworkAccessManager network;
    enqueueSuccessfulHandshake(server,userResponse("admin",false,false,false,false));
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto provider=qobject_cast<IScrobbleProviderV2 *>(&session); QVERIFY(provider);
    NavidromeSourcePlugin plugin;
    QCOMPARE(plugin.descriptor().declaredActions.value(SourceActionV2::Scrobble).state,AvailabilityV2::Available);
    session.open(); QTRY_COMPARE(session.state(),SourceSessionStateV2::Ready);
    const auto before=session.capabilities(); const auto owner=session.identity();
    QCOMPARE(owner.sourcePluginId,plugin.descriptor().sourceId);
    QCOMPARE(before.action(SourceActionV2::Scrobble).state,AvailabilityV2::Available);
    const auto track=NavidromeMappers::song(QJsonObject{{"id","42 & 7"},{"title","Track"}},owner).ref;
    QSignalSpy started(&session,&IMusicSourceSessionV2::requestStarted);
    QSignalSpy done(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    QSignalSpy caps(&session,&IMusicSourceSessionV2::capabilitiesChanged);
    for (bool submission : {false,true}) {
        const qint64 positionMs=submission ? 9876543210ll : 0;
        const auto count=server.requests().size();
        server.enqueueHeld(QJsonDocument(subsonicOk()).toJson(),"application/json");
        const auto id=provider->scrobble(track,positionMs,submission);
        QCOMPARE(started.last()[0].toUuid(),id);
        QTRY_COMPARE(server.requests().size(),count+1); QCOMPARE(done.size(),0);
        QCOMPARE(server.requests().last().url.path(),QString("/rest/scrobble.view"));
        const QUrlQuery wire(server.requests().last().url);
        QCOMPARE(wire.allQueryItemValues("id"),QStringList({"42 & 7"}));
        QCOMPARE(wire.allQueryItemValues("submission"),QStringList({submission ? "true" : "false"}));
        QVERIFY(!wire.hasQueryItem("time")); QVERIFY(!wire.hasQueryItem("position"));
        QVERIFY(!wire.hasQueryItem("positionMs"));
        server.releaseHeld(); QTRY_COMPARE(done.size(),1);
        const auto completion=done.takeFirst(); QCOMPARE(completion[0].toUuid(),id);
        const auto result=completion[1].value<ActionResultV2>();
        QCOMPARE(result.action,SourceActionV2::Scrobble); QCOMPARE(result.subject,track);
        QCOMPARE(result.payload.keys(),QStringList({"positionMs","submission"}));
        QCOMPARE(result.payload.value("positionMs").metaType().id(),int(QMetaType::LongLong));
        QCOMPARE(result.payload.value("positionMs").toLongLong(),positionMs);
        QCOMPARE(result.payload.value("submission").metaType().id(),int(QMetaType::Bool));
        QCOMPARE(result.payload.value("submission").toBool(),submission);
    }
    QCOMPARE(failed.size(),0);
    const auto count=server.requests().size();
    for (int field=0;field<8;++field) {
        auto invalid=track;
        if (field==0) invalid.sourcePluginId="foreign";
        if (field==1) invalid.sourcePluginId=plugin.descriptor().pluginPackageId;
        if (field==2) invalid.sourceInstanceId="foreign";
        if (field==3) invalid.accountId="foreign";
        if (field==4) invalid.entityType=MediaEntityTypeV2::Album;
        if (field==5) invalid.entityId="";
        if (field==6) invalid.entityId="  ";
        const auto id=provider->scrobble(invalid,field==7 ? -1 : 0,true);
        QTRY_COMPARE(failed.size(),1);
        const auto failure=failed.takeFirst(); QCOMPARE(failure[0].toUuid(),id);
        QCOMPARE(failure[1].value<SourceErrorV2>().kind,SourceErrorKindV2::InvalidRequest);
    }
    QCOMPARE(server.requests().size(),count);
    server.enqueue(subsonicError(0,"Failure"));
    const auto failureId=provider->scrobble(track,0,false);
    QTRY_COMPARE(failed.size(),1); QCOMPARE(failed.takeFirst()[0].toUuid(),failureId);
    QCOMPARE(done.size(),0); QCOMPARE(caps.size(),0);
    server.enqueue(subsonicError(50,"Denied"));
    const auto deniedId=provider->scrobble(track,1,true);
    QTRY_COMPARE(failed.size(),1);
    const auto denial=failed.takeFirst(); QCOMPARE(denial[0].toUuid(),deniedId);
    QCOMPARE(denial[1].value<SourceErrorV2>().kind,SourceErrorKindV2::Authorization);
    QCOMPARE(done.size(),0); QCOMPARE(caps.size(),1);
    QCOMPARE(session.capabilities().serverActions,before.serverActions);
    QCOMPARE(session.capabilities().action(SourceActionV2::Scrobble).state,AvailabilityV2::Forbidden);
    QCOMPARE(session.capabilities().accountAction(SourceActionV2::Scrobble).reasonKey,QString("source.permission.scrobble"));
    for (auto action:allSourceActions)
        if (action!=SourceActionV2::Scrobble)
            QCOMPARE(session.capabilities().accountAction(action),before.accountAction(action));
    const auto deniedCount=server.requests().size();
    provider->scrobble(track,0,false); QTRY_COMPARE(failed.size(),1); failed.clear();
    QCOMPARE(server.requests().size(),deniedCount);
    enqueueSuccessfulHandshake(server,userResponse()); session.open();
    QTRY_COMPARE(session.state(),SourceSessionStateV2::Ready);
    QCOMPARE(session.capabilities().action(SourceActionV2::Scrobble).state,AvailabilityV2::Available);
    for (bool close : {false,true}) {
        const auto requestCount=server.requests().size();
        const auto connection=connect(&session,&IMusicSourceSessionV2::requestStarted,&session,
            [&](QUuid id){ if (close) session.close(); else session.cancel(id); });
        provider->scrobble(track,0,false); disconnect(connection);
        QTest::qWait(20); QCOMPARE(server.requests().size(),requestCount);
    }
    for (bool close : {false,true}) {
        const auto requestCount=server.requests().size();
        server.enqueueHeld(QJsonDocument(subsonicOk()).toJson(),"application/json");
        const auto id=provider->scrobble(track,0,true);
        QTRY_COMPARE(server.requests().size(),requestCount+1);
        if (close) session.close(); else session.cancel(id);
        server.releaseHeld(); QTest::qWait(20);
    }
    QCOMPARE(done.size(),0); QCOMPARE(failed.size(),0);
}

void NavidromeSourceTest::task11Actions_data()
{
    QTest::addColumn<int>("action"); QTest::addColumn<QString>("endpoint");
    const QList<QPair<SourceActionV2,QString>> cases{
        {SourceActionV2::Favorite,"star"},{SourceActionV2::Unfavorite,"unstar"},
        {SourceActionV2::Rating,"setRating"},{SourceActionV2::CreatePlaylist,"createPlaylist"},
        {SourceActionV2::UpdatePlaylist,"updatePlaylist"},{SourceActionV2::DeletePlaylist,"deletePlaylist"},
        {SourceActionV2::SavePlayQueue,"savePlayQueue"},{SourceActionV2::FetchPlayQueue,"getPlayQueue"},
        {SourceActionV2::FetchBookmarks,"getBookmarks"},{SourceActionV2::CreateBookmark,"createBookmark"},
        {SourceActionV2::DeleteBookmark,"deleteBookmark"}};
    for (const auto &c:cases) QTest::newRow(qPrintable(c.second)) << int(c.first) << c.second;
}
void NavidromeSourceTest::task11Actions()
{
    QFETCH(int,action); QFETCH(QString,endpoint);
    FakeNavidromeServer server; QVERIFY(server.start());
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto f=qobject_cast<IFavoriteProviderV2 *>(&session); QVERIFY(f);
    auto r=qobject_cast<IRatingProviderV2 *>(&session); QVERIFY(r);
    auto p=qobject_cast<IPlaylistProviderV2 *>(&session); QVERIFY(p);
    auto q=qobject_cast<IPlayQueueProviderV2 *>(&session); QVERIFY(q);
    auto b=qobject_cast<IBookmarkProviderV2 *>(&session); QVERIFY(b);
    const auto owner=session.identity();
    MediaRefV2 track{owner.sourcePluginId,owner.sourceInstanceId,owner.accountId,MediaEntityTypeV2::Track,"42"};
    auto list=track; list.entityType=MediaEntityTypeV2::Playlist; list.entityId="p1";
    server.enqueue(subsonicOk({{"playlist",QJsonObject{{"id","created"},{"name","Server name"}}},
        {"playQueue",QJsonObject{}},{"bookmarks",QJsonObject{}}}));
    QSignalSpy started(&session,&IMusicSourceSessionV2::requestStarted);
    QSignalSpy done(&session,&IMusicSourceSessionV2::actionCompleted);
    QUuid id;
    switch (SourceActionV2(action)) {
    case SourceActionV2::Favorite: id=f->setFavorite(track,true); break;
    case SourceActionV2::Unfavorite: id=f->setFavorite(track,false); break;
    case SourceActionV2::Rating: id=r->setRating(track,0); break;
    case SourceActionV2::CreatePlaylist: id=p->createPlaylist(" New ",{track,track}); break;
    case SourceActionV2::UpdatePlaylist: id=p->updatePlaylist(list,{{track},{},{}}); break;
    case SourceActionV2::DeletePlaylist: id=p->deletePlaylist(list); break;
    case SourceActionV2::SavePlayQueue: id=q->savePlayQueue({}, {},0); break;
    case SourceActionV2::FetchPlayQueue: id=q->fetchPlayQueue(); break;
    case SourceActionV2::FetchBookmarks: id=b->fetchBookmarks(); break;
    case SourceActionV2::CreateBookmark: id=b->createBookmark(track,9876543210ll,"a & b"); break;
    case SourceActionV2::DeleteBookmark: id=b->deleteBookmark(track); break;
    default: QFAIL("unexpected action");
    }
    QCOMPARE(started.size(),1); QCOMPARE(started.first().first().toUuid(),id);
    QTRY_COMPARE(done.size(),1); QCOMPARE(done.first().first().toUuid(),id);
    const auto result=done.first().at(1).value<ActionResultV2>();
    QCOMPARE(int(result.action),action);
    QCOMPARE(result.subject.sourceInstanceId,owner.sourceInstanceId);
    QCOMPARE(result.subject.sourcePluginId,owner.sourcePluginId);
    QCOMPARE(result.subject.accountId,owner.accountId);
    QCOMPARE(server.requests().size(),1);
    QCOMPARE(server.requests().first().url.path(),"/rest/"+endpoint+".view");
    const QUrlQuery wire(server.requests().first().url);
    if (result.action==SourceActionV2::CreatePlaylist) {
        QCOMPARE(result.subject.entityId,QString("created"));
        QCOMPARE(result.payload.value("name").toString(),QString("Server name"));
        QCOMPARE(wire.allQueryItemValues("songId"),QStringList({"42","42"}));
    } else if (result.action==SourceActionV2::CreateBookmark) {
        QCOMPARE(result.payload.value("positionMs").metaType().id(),int(QMetaType::LongLong));
        QCOMPARE(result.payload.value("positionMs").toLongLong(),9876543210ll);
        QCOMPARE(wire.queryItemValue("comment"),QString("a & b"));
    } else if (result.action==SourceActionV2::SavePlayQueue) QVERIFY(!wire.hasQueryItem("current"));
    else if (result.action==SourceActionV2::Rating) QCOMPARE(result.payload.value("rating"),QVariant(0));
    else if (result.action==SourceActionV2::Favorite || result.action==SourceActionV2::Unfavorite)
        QCOMPARE(result.payload.value("favorite"),QVariant(result.action==SourceActionV2::Favorite));
}
void NavidromeSourceTest::task11ValidationAndLifecycle()
{
    FakeNavidromeServer server; QVERIFY(server.start()); QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto f=qobject_cast<IFavoriteProviderV2 *>(&session); QVERIFY(f);
    auto r=qobject_cast<IRatingProviderV2 *>(&session); QVERIFY(r);
    auto p=qobject_cast<IPlaylistProviderV2 *>(&session); QVERIFY(p);
    auto q=qobject_cast<IPlayQueueProviderV2 *>(&session); QVERIFY(q);
    auto b=qobject_cast<IBookmarkProviderV2 *>(&session); QVERIFY(b);
    const auto owner=session.identity();
    MediaRefV2 track{owner.sourcePluginId,owner.sourceInstanceId,owner.accountId,MediaEntityTypeV2::Track,"42"};
    auto list=track; list.entityType=MediaEntityTypeV2::Playlist;
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    QSignalSpy done(&session,&IMusicSourceSessionV2::actionCompleted);
    for (int field=0;field<3;++field) {
        auto foreign=track;
        if (field==0) foreign.sourcePluginId="foreign";
        if (field==1) foreign.sourceInstanceId="foreign";
        if (field==2) foreign.accountId="foreign";
        p->createPlaylist("name",{foreign}); f->setFavorite(foreign,true);
        q->savePlayQueue({foreign},foreign,0); b->deleteBookmark(foreign);
    }
    r->setRating(track,-1); r->setRating(track,6); r->setRating(list,3);
    b->createBookmark(track,-1,{}); p->createPlaylist("  ",{});
    p->updatePlaylist(list,{{},{},"  "}); p->updatePlaylist(list,{{},{-1},{}});
    p->updatePlaylist(list,{{},{2,2},{}}); p->updatePlaylist(list,{});
    q->savePlayQueue({track},list,0); q->savePlayQueue({track},track,-1);
    f->setFavorite(list,true); b->createBookmark(list,0,{});
    QTRY_COMPARE(failed.size(),25); QCOMPARE(server.requests().size(),0);
    auto connection=connect(&session,&IMusicSourceSessionV2::requestStarted,&session,
        [&](QUuid id){session.cancel(id);});
    f->setFavorite(track,true); disconnect(connection);
    connection=connect(&session,&IMusicSourceSessionV2::requestStarted,&session,
        [&](QUuid){session.close();});
    q->fetchPlayQueue(); disconnect(connection);
    QTest::qWait(20); QCOMPARE(server.requests().size(),0); QCOMPARE(done.size(),0);
    server.enqueueHeld(QJsonDocument(subsonicOk()).toJson(),"application/json");
    const auto id=f->setFavorite(track,true);
    QTRY_COMPARE(server.requests().size(),1); session.cancel(id); server.releaseHeld();
    QTest::qWait(20); QCOMPARE(done.size(),0); QCOMPARE(failed.size(),25);
}
void NavidromeSourceTest::task11PermissionDowngrade()
{
    FakeNavidromeServer server; QVERIFY(server.start()); QNetworkAccessManager network;
    enqueueSuccessfulHandshake(server,userResponse());
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto p=qobject_cast<IPlaylistProviderV2 *>(&session); QVERIFY(p);
    session.open(); QTRY_COMPARE(session.state(),SourceSessionStateV2::Ready);
    const auto before=session.capabilities(); const auto owner=session.identity();
    MediaRefV2 track{owner.sourcePluginId,owner.sourceInstanceId,owner.accountId,MediaEntityTypeV2::Track,"42"};
    auto list=track; list.entityType=MediaEntityTypeV2::Playlist;
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    QSignalSpy caps(&session,&IMusicSourceSessionV2::capabilitiesChanged);
    server.enqueue(subsonicError(50,"Denied"));
    p->updatePlaylist(list,{{track},{0},{}});
    QTRY_COMPARE(failed.size(),1); QCOMPARE(caps.size(),1);
    QCOMPARE(session.capabilities().serverActions,before.serverActions);
    QCOMPARE(session.capabilities().action(SourceActionV2::AddPlaylistTracks).state,AvailabilityV2::Forbidden);
    QCOMPARE(session.capabilities().action(SourceActionV2::RemovePlaylistTracks).state,AvailabilityV2::Forbidden);
    QCOMPARE(session.capabilities().accountAction(SourceActionV2::UpdatePlaylist),before.accountAction(SourceActionV2::UpdatePlaylist));
    QCOMPARE(session.capabilities().accountAction(SourceActionV2::AddPlaylistTracks).reasonKey,QString("source.permission.addPlaylistTracks"));
    QTemporaryDir dir; server.enqueue(subsonicError(50,"Denied"));
    session.download(track,QUrl::fromLocalFile(dir.filePath("download")));
    QTRY_COMPARE(failed.size(),2);
    QCOMPARE(session.capabilities().action(SourceActionV2::Download).state,AvailabilityV2::Forbidden);
    QCOMPARE(session.capabilities().serverActions,before.serverActions);
    enqueueSuccessfulHandshake(server,userResponse()); session.open();
    QTRY_COMPARE(session.state(),SourceSessionStateV2::Ready);
    QCOMPARE(session.capabilities().accountActions,before.accountActions);
}
void NavidromeSourceTest::task11PlaylistsPage()
{
    FakeNavidromeServer server; QVERIFY(server.start()); QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    const auto response=subsonicOk({{"playlists",QJsonObject{{"playlist",QJsonArray{
        QJsonObject{{"id","p1"},{"name","One"}},QJsonObject{{"id","p2"},{"name","Two"}}}}}}});
    server.enqueue(response); server.enqueue(response);
    PageQueryV2 query; query.page=MusicPageKindV2::Favorites; query.section=PageSectionKindV2::Playlists;
    query.scope.sourceInstanceId=session.identity().sourceInstanceId; query.limit=1;
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    session.fetchPage(query); QTRY_COMPARE(pages.size(),1);
    auto section=pages.last().at(1).value<PageResultV2>().sections.first();
    QCOMPARE(section.kind,PageSectionKindV2::Playlists); QCOMPARE(section.items.first().ref.entityId,QString("p1"));
    QVERIFY(section.hasMore); query.cursor=section.nextCursor;
    session.fetchPage(query); QTRY_COMPARE(pages.size(),2);
    section=pages.last().at(1).value<PageResultV2>().sections.first();
    QCOMPARE(section.items.first().ref.entityId,QString("p2")); QVERIFY(!section.hasMore);
    QCOMPARE(server.requests().first().url.path(),QString("/rest/getPlaylists.view"));
    QVERIFY(!QUrlQuery(server.requests().first().url).hasQueryItem("username"));
    QCOMPARE(section.items.first().availableActions.value(SourceActionV2::UpdatePlaylist).state,AvailabilityV2::Available);
}

void NavidromeSourceTest::task11RejectsMalformedPlaylists()
{
    FakeNavidromeServer server; QVERIFY(server.start()); QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto query=pageQuery(PageSectionKindV2::Playlists);
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    const QList<QJsonValue> invalid{QJsonValue(),QString("bad"),
        QJsonObject{{"playlist","bad"}}, QJsonObject{{"playlist",QJsonArray{42}}},
        QJsonObject{{"playlist",QJsonArray{QJsonObject{{"name","Broken"}}}}},
        QJsonObject{{"playlist",QJsonArray{QJsonObject{{"id","  "}}}}},
        QJsonObject{{"playlist",QJsonArray{QJsonObject{{"id",42}}}}}};
    for (const auto &value:invalid) {
        const int expected=failed.size()+1;
        server.enqueue(subsonicOk({{"playlists",value}})); session.fetchPage(query);
        QTRY_COMPARE(failed.size(),expected);
        QCOMPARE(failed.last()[1].value<SourceErrorV2>().kind,SourceErrorKindV2::InvalidRequest);
        QCOMPARE(pages.size(),0);
    }
    for (const auto &value:{QJsonObject{},QJsonObject{{"playlist",QJsonArray{}}}}) {
        const int expected=pages.size()+1;
        server.enqueue(subsonicOk({{"playlists",value}})); session.fetchPage(query);
        QTRY_COMPARE(pages.size(),expected);
        QVERIFY(pages.last()[1].value<PageResultV2>().sections.first().items.isEmpty());
    }
}

void NavidromeSourceTest::task11ProvidersAndWireContracts()
{
    FakeNavidromeServer server;
    QVERIFY(server.start());
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    auto favorite = qobject_cast<IFavoriteProviderV2 *>(&session);
    auto rating = qobject_cast<IRatingProviderV2 *>(&session);
    auto playlists = qobject_cast<IPlaylistProviderV2 *>(&session);
    auto queue = qobject_cast<IPlayQueueProviderV2 *>(&session);
    auto bookmarks = qobject_cast<IBookmarkProviderV2 *>(&session);
    QVERIFY(favorite); QVERIFY(rating); QVERIFY(playlists); QVERIFY(queue); QVERIFY(bookmarks);
    const auto owner = session.identity();
    MediaRefV2 track{owner.sourcePluginId,owner.sourceInstanceId,owner.accountId,MediaEntityTypeV2::Track,"42"};
    MediaRefV2 playlist = track; playlist.entityType=MediaEntityTypeV2::Playlist; playlist.entityId="p1";
    QSignalSpy started(&session,&IMusicSourceSessionV2::requestStarted);
    QSignalSpy done(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    server.enqueue(subsonicOk());
    const auto id=favorite->setFavorite(track,true);
    QCOMPARE(started.size(),1); QCOMPARE(started.last().first().toUuid(),id);
    QTRY_COMPARE(done.size(),1);
    QCOMPARE(done.last().first().toUuid(),id);
    QCOMPARE(done.last().at(1).value<ActionResultV2>().payload.value("favorite"),QVariant(true));
    QCOMPARE(server.requests().last().url.path(),QString("/rest/star.view"));
    server.enqueue(subsonicOk());
    PlaylistChangeV2 change{{track,track},{2,0}," Renamed "};
    playlists->updatePlaylist(playlist,change);
    QTRY_COMPARE(done.size(),2);
    QCOMPARE(done.last().at(1).value<ActionResultV2>().action,SourceActionV2::UpdatePlaylist);
    QCOMPARE(done.last().at(1).value<ActionResultV2>().subject,playlist);
    const QUrlQuery query(server.requests().last().url);
    QCOMPARE(query.allQueryItemValues("songIdToAdd"),QStringList({"42","42"}));
    QCOMPARE(query.allQueryItemValues("songIndexToRemove"),QStringList({"2","0"}));
    QCOMPARE(query.queryItemValue("name"),QString("Renamed"));
    change.trackIndexesToRemove={2,2};
    playlists->updatePlaylist(playlist,change);
    QTRY_COMPARE(failed.size(),1); QCOMPARE(server.requests().size(),2);
    server.enqueue(subsonicOk());
    queue->savePlayQueue({track,track},track,1234);
    QTRY_COMPARE(done.size(),3);
    QCOMPARE(QUrlQuery(server.requests().last().url).allQueryItemValues("id"),QStringList({"42","42"}));
    server.enqueue(subsonicOk({{"playQueue",QJsonObject{{"current","42"},{"position",1234},
        {"entry",QJsonArray{QJsonObject{{"id","42"},{"url","secret"}},QJsonObject{{"id","42"}}}}}}}));
    queue->fetchPlayQueue();
    QTRY_COMPARE(done.size(),4);
    const auto payload=done.last().at(1).value<ActionResultV2>().payload;
    QCOMPARE(payload.keys(),QStringList({"current","items","positionMs"}));
    QCOMPARE(payload.value("items").toList().size(),2);
    QCOMPARE(mediaRefV2FromVariantMap(payload.value("items").toList().first().toMap()),track);
    QCOMPARE(payload.value("positionMs").toLongLong(),1234);
    server.enqueue(subsonicOk({{"bookmarks",QJsonObject{{"bookmark",QJsonArray{QJsonObject{
        {"entry",QJsonObject{{"id","42"},{"url","secret"}}},{"position",99},{"comment","note"},{"password","secret"}}}}}}}));
    bookmarks->fetchBookmarks();
    QTRY_COMPARE(done.size(),5);
    const auto bookmark=done.last().at(1).value<ActionResultV2>().payload.value("bookmarks").toList().first().toMap();
    QCOMPARE(bookmark.keys(),QStringList({"comment","media","positionMs"}));
    QCOMPARE(mediaRefV2FromVariantMap(bookmark.value("media").toMap()),track);
    QCOMPARE(bookmark.value("positionMs").toLongLong(),99);
}

void NavidromeSourceTest::descriptorMatchesImplementedV2Providers()
{
    NavidromeSourcePlugin plugin;
    const SourceDescriptorV2 descriptor = plugin.descriptor();
    IMusicSourceSessionV2 *session = plugin.createSession(configuration(8533), &plugin);
    QVERIFY(session != nullptr);
    QVERIFY(qobject_cast<IPlaybackProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IFavoriteProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IRatingProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IScrobbleProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IPlaylistProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IDownloadProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IPlayQueueProviderV2 *>(session) != nullptr);
    QVERIFY(qobject_cast<IBookmarkProviderV2 *>(session) != nullptr);
    QCOMPARE(descriptor.declaredActions.size(), implementedSourceActions.size());
    for (SourceActionV2 action : {SourceActionV2::Play, SourceActionV2::Artwork,
                                  SourceActionV2::Lyrics, SourceActionV2::Download})
        QCOMPARE(descriptor.declaredActions.value(action).state, AvailabilityV2::Available);
    for (SourceActionV2 action : allSourceActions)
        if (!descriptor.declaredActions.contains(action))
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
    // Advertising a Task 11 action here lets the router expose an absent provider method.
    for (SourceActionV2 action : allSourceActions)
        QCOMPARE(capabilities.serverAction(action).state,
                 implementedSourceActions.contains(action) ? AvailabilityV2::Available
                                                           : AvailabilityV2::Unsupported);
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
                                  SourceActionV2::Download}) {
        QCOMPARE(caps.serverAction(action).state, AvailabilityV2::Available);
        QCOMPARE(caps.accountAction(action).state, AvailabilityV2::Unavailable);
        QCOMPARE(caps.action(action).state, AvailabilityV2::Unavailable);
    }
    QCOMPARE(caps.accountAction(SourceActionV2::Lyrics).state, AvailabilityV2::Available);
    for (SourceActionV2 action : allSourceActions)
        if (!implementedSourceActions.contains(action))
            QCOMPARE(caps.action(action).state,AvailabilityV2::Unsupported);
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
        TrackRef{QStringLiteral("navidrome"), QStringLiteral("song-1")});
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
    session.fetchArtwork(TrackRef{QStringLiteral("navidrome"), QStringLiteral("cover-1")});
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
        TrackRef{QStringLiteral("navidrome"), QStringLiteral("song-1")});
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
        TrackRef{QStringLiteral("navidrome"), QStringLiteral("song-1")});
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
    session.fetchLyrics(TrackRef{QStringLiteral("navidrome"), QStringLiteral("song-1")});
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(1).url.path(), QStringLiteral("/rest/getLyrics.view"));
}

void NavidromeSourceTest::pageEndpoint_data()
{
    QTest::addColumn<PageSectionKindV2>("section");
    QTest::addColumn<QString>("endpoint");
    QTest::addColumn<QString>("type");
    QTest::newRow("random") << PageSectionKindV2::Random << "getAlbumList2" << "random";
    QTest::newRow("newest") << PageSectionKindV2::Newest << "getAlbumList2" << "newest";
    QTest::newRow("recent") << PageSectionKindV2::RecentlyPlayed << "getAlbumList2" << "recent";
    QTest::newRow("frequent") << PageSectionKindV2::FrequentlyPlayed << "getAlbumList2" << "frequent";
    QTest::newRow("highest") << PageSectionKindV2::HighestRated << "getAlbumList2" << "highest";
    QTest::newRow("genres") << PageSectionKindV2::Genres << "getGenres" << "";
    QTest::newRow("artists") << PageSectionKindV2::Artists << "getArtists" << "";
}

void NavidromeSourceTest::pageEndpoint()
{
    // A wrong dispatch table silently feeds a section from the wrong Subsonic collection.
    QFETCH(PageSectionKindV2, section); QFETCH(QString, endpoint); QFETCH(QString, type);
    FakeNavidromeServer server; QVERIFY(server.start());
    if (endpoint == QStringLiteral("getAlbumList2"))
        server.enqueue(subsonicOk({{"albumList2",QJsonObject{{"album",QJsonArray{
            QJsonObject{{"id","album-1"},{"name","Album"},{"artist","Artist"}}}}}}}));
    else if (endpoint == QStringLiteral("getGenres"))
        server.enqueue(subsonicOk({{"genres",QJsonObject{{"genre",QJsonArray{
            QJsonObject{{"value","Jazz"},{"songCount",2},{"albumCount",1}}}}}}}));
    else
        server.enqueue(subsonicOk({{"artists",QJsonObject{{"index",QJsonArray{
            QJsonObject{{"name","A"},{"artist",QJsonArray{QJsonObject{{"id","artist-1"},{"name","Artist"}}}}}}}}}}));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto *provider=qobject_cast<IPageProviderV2 *>(&session); QVERIFY(provider);
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    const auto id=provider->fetchPage(pageQuery(section)); QTRY_COMPARE(pages.size(),1);
    QCOMPARE(pages[0][0].toUuid(),id);
    const auto result=qvariant_cast<PageResultV2>(pages[0][1]);
    QCOMPARE(result.sections.size(),1); QCOMPARE(result.sections[0].kind,section);
    QCOMPARE(result.sections[0].items.size(),1);
    QCOMPARE(result.sections[0].items[0].ref.sourceInstanceId,QString("navidrome/admin"));
    QCOMPARE(server.requests().size(),1);
    QCOMPARE(server.requests()[0].url.path(),QString("/rest/")+endpoint+QString(".view"));
    QCOMPARE(QUrlQuery(server.requests()[0].url).queryItemValue("type"),type);
    if (endpoint==QStringLiteral("getAlbumList2"))
        QCOMPARE(QUrlQuery(server.requests()[0].url).queryItemValue("size"),QString("2"));
}

void NavidromeSourceTest::drillDownUsesTypedFilterEndpoint_data()
{
    QTest::addColumn<QString>("filterKey"); QTest::addColumn<QString>("endpoint");
    QTest::newRow("artist") << "artistId" << "getArtist";
    QTest::newRow("album") << "albumId" << "getAlbum";
    QTest::newRow("song") << "songId" << "getSong";
    QTest::newRow("genre") << "genre" << "getSongsByGenre";
    QTest::newRow("playlist") << "playlistId" << "getPlaylist";
}

void NavidromeSourceTest::drillDownUsesTypedFilterEndpoint()
{
    // Treating typed IDs interchangeably sends valid IDs to incompatible endpoints.
    QFETCH(QString,filterKey); QFETCH(QString,endpoint);
    FakeNavidromeServer server; QVERIFY(server.start());
    QJsonObject fields;
    const QJsonObject album{{"id","a1"},{"name","Album"}};
    const QJsonObject song{{"id","s1"},{"title","Song"}};
    if (endpoint==QStringLiteral("getArtist"))
        fields.insert("artist",QJsonObject{{"album",QJsonArray{album}}});
    else if (endpoint==QStringLiteral("getAlbum"))
        fields.insert("album",QJsonObject{{"song",QJsonArray{song}}});
    else if (endpoint==QStringLiteral("getSong"))
        fields.insert("song",song);
    else if (endpoint==QStringLiteral("getSongsByGenre"))
        fields.insert("songsByGenre",QJsonObject{{"song",QJsonArray{song}}});
    else
        fields.insert("playlist",QJsonObject{{"id","p1"},{"entry",QJsonArray{song}}});
    server.enqueue(subsonicOk(fields));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    PageQueryV2 query=pageQuery(filterKey==QStringLiteral("artistId")
                                    ? PageSectionKindV2::Albums : PageSectionKindV2::Tracks);
    query.filters={{filterKey,filterKey==QStringLiteral("genre")?QString("Jazz"):QString("p1")}};
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query); QTRY_COMPARE(pages.size(),1);
    QCOMPARE(server.requests()[0].url.path(),QString("/rest/")+endpoint+QString(".view"));
}

void NavidromeSourceTest::favoritesMapsSongsAlbumsAndArtists()
{
    // Flattening getStarred2 loses the typed three-section favorites contract.
    FakeNavidromeServer server; QVERIFY(server.start());
    QJsonObject starred;
    starred.insert("song",QJsonArray{QJsonObject{{"id","s1"},{"title","Song"}}});
    starred.insert("album",QJsonArray{QJsonObject{{"id","a1"},{"name","Album"}}});
    starred.insert("artist",QJsonArray{QJsonObject{{"id","r1"},{"name","Artist"}}});
    server.enqueue(subsonicOk({{"starred2",starred}}));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    PageQueryV2 query=pageQuery(PageSectionKindV2::FavoriteTracks); query.page=MusicPageKindV2::Favorites;
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query); QTRY_COMPARE(pages.size(),1);
    const auto result=qvariant_cast<PageResultV2>(pages[0][1]);
    QCOMPARE(result.sections.size(),3);
    QCOMPARE(result.sections[0].kind,PageSectionKindV2::FavoriteTracks);
    QCOMPARE(result.sections[1].kind,PageSectionKindV2::FavoriteAlbums);
    QCOMPARE(result.sections[2].kind,PageSectionKindV2::FavoriteArtists);
}

void NavidromeSourceTest::favoritesContinuationIsSectionSpecific()
{
    // Reusing one favorites offset can advance categories the user did not continue.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    QJsonObject first;
    first.insert("song",QJsonArray{QJsonObject{{"id","s1"}},QJsonObject{{"id","s2"}}});
    first.insert("album",QJsonArray{QJsonObject{{"id","a1"}},QJsonObject{{"id","a2"}}});
    first.insert("artist",QJsonArray{QJsonObject{{"id","r1"}},QJsonObject{{"id","r2"}}});
    server.enqueue(subsonicOk({{"starred2",first}}));
    server.enqueue(subsonicOk({{"starred2",QJsonObject{{"album",QJsonArray{
        QJsonObject{{"id","a3"},{"name","Album 3"}}}}}}}));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    PageQueryV2 query=pageQuery(PageSectionKindV2::FavoriteTracks);
    query.page=MusicPageKindV2::Favorites;
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query);
    QTRY_COMPARE(pages.size(),1);
    const PageResultV2 initial=qvariant_cast<PageResultV2>(pages.takeFirst().at(1));
    QCOMPARE(initial.sections.size(),3);
    for (const PageSectionV2 &section:initial.sections) {
        QVERIFY(section.hasMore);
        QCOMPARE(section.nextCursor,QStringLiteral("2"));
    }
    query.section=PageSectionKindV2::FavoriteAlbums;
    query.cursor=QStringLiteral("2");
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query);
    QTRY_COMPARE(pages.size(),1);
    const PageResultV2 continuation=qvariant_cast<PageResultV2>(pages.takeFirst().at(1));
    QCOMPARE(continuation.sections.size(),1);
    QCOMPARE(continuation.sections.constFirst().kind,PageSectionKindV2::FavoriteAlbums);
    QCOMPARE(continuation.sections.constFirst().items.size(),1);
    QCOMPARE(continuation.sections.constFirst().items.constFirst().ref.entityId,QStringLiteral("a3"));
    const QUrlQuery sent(server.requests().constLast().url);
    QCOMPARE(sent.queryItemValue(QStringLiteral("songCount")),QStringLiteral("0"));
    QCOMPARE(sent.queryItemValue(QStringLiteral("albumCount")),QStringLiteral("2"));
    QCOMPARE(sent.queryItemValue(QStringLiteral("albumOffset")),QStringLiteral("2"));
    QCOMPARE(sent.queryItemValue(QStringLiteral("artistCount")),QStringLiteral("0"));
}

void NavidromeSourceTest::searchContinuationKeepsCategoryOffsetsIndependent()
{
    // Flattening mixed search categories advances one shared offset and loses prefetched rows.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    const auto result = [](QJsonArray songs, QJsonArray albums, QJsonArray artists) {
        return subsonicOk({{"searchResult3", QJsonObject{{"song", songs},
                                                           {"album", albums},
                                                           {"artist", artists}}}});
    };
    server.enqueue(result({QJsonObject{{"id","s1"},{"title","Song 1"}},
                           QJsonObject{{"id","s2"},{"title","Song 2"}}},
                          {QJsonObject{{"id","a1"},{"name","Album 1"}},
                           QJsonObject{{"id","a2"},{"name","Album 2"}}},
                          {QJsonObject{{"id","r1"},{"name","Artist 1"}},
                           QJsonObject{{"id","r2"},{"name","Artist 2"}}}));
    server.enqueue(result({QJsonObject{{"id","s3"},{"title","Song 3"}},
                           QJsonObject{{"id","s4"},{"title","Song 4"}}}, {}, {}));
    server.enqueue(result({}, {QJsonObject{{"id","a3"},{"name","Album 3"}},
                               QJsonObject{{"id","a4"},{"name","Album 4"}}}, {}));
    server.enqueue(result({}, {}, {QJsonObject{{"id","r3"},{"name","Artist 3"}},
                                   QJsonObject{{"id","r4"},{"name","Artist 4"}}}));

    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy pages(&session, &IMusicSourceSessionV2::pageReady);
    PageQueryV2 query = pageQuery(PageSectionKindV2::SearchResults);
    query.page = MusicPageKindV2::Search;
    query.searchText = QStringLiteral("mix");
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query);
    QTRY_COMPARE(pages.size(), 1);
    PageResultV2 page = qvariant_cast<PageResultV2>(pages.takeFirst().at(1));
    QCOMPARE(page.sections.size(), 3);

    QHash<MediaEntityTypeV2, QStringList> ids;
    for (const PageSectionV2 &section : page.sections) {
        QCOMPARE(section.items.size(), 2);
        QVERIFY(section.hasMore);
        QCOMPARE(section.nextCursor, QStringLiteral("2"));
        for (const MediaItemV2 &item : section.items)
            ids[item.ref.entityType].append(item.ref.entityId);
    }

    const QList<PageSectionKindV2> continuations{PageSectionKindV2::Tracks,
                                                  PageSectionKindV2::Albums,
                                                  PageSectionKindV2::Artists};
    for (PageSectionKindV2 section : continuations) {
        query.section = section;
        query.cursor = QStringLiteral("2");
        qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query);
        QTRY_COMPARE(pages.size(), 1);
        page = qvariant_cast<PageResultV2>(pages.takeFirst().at(1));
        QCOMPARE(page.sections.size(), 1);
        QCOMPARE(page.sections.constFirst().kind, section);
        for (const MediaItemV2 &item : page.sections.constFirst().items)
            ids[item.ref.entityType].append(item.ref.entityId);
    }

    QCOMPARE(ids.value(MediaEntityTypeV2::Track), QStringList({"s1","s2","s3","s4"}));
    QCOMPARE(ids.value(MediaEntityTypeV2::Album), QStringList({"a1","a2","a3","a4"}));
    QCOMPARE(ids.value(MediaEntityTypeV2::Artist), QStringList({"r1","r2","r3","r4"}));
    QCOMPARE(server.requests().size(), 4);
    for (int index = 1; index < 4; ++index) {
        const QUrlQuery sent(server.requests().at(index).url);
        QCOMPARE(sent.queryItemValue(QStringLiteral("songCount")), index == 1 ? "2" : "0");
        QCOMPARE(sent.queryItemValue(QStringLiteral("albumCount")), index == 2 ? "2" : "0");
        QCOMPARE(sent.queryItemValue(QStringLiteral("artistCount")), index == 3 ? "2" : "0");
    }
}

void NavidromeSourceTest::playlistTracksCarryAbsoluteOccurrenceMetadata()
{
    // Using a rendered row or track number corrupts occurrence identity after slicing.
    FakeNavidromeServer server; QVERIFY(server.start());
    QJsonArray entries;
    for (int index=0;index<3;++index)
        entries.append(QJsonObject{{"id",index==1?"same":"song"},{"title",QString("Song %1").arg(index)},
                                   {"track",99},{"isrc","CN-A01-24-00001"}});
    server.enqueue(subsonicOk({{"playlist",QJsonObject{{"id","p1"},{"entry",entries}}}}));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    auto query=pageQuery(PageSectionKindV2::Tracks); query.limit=1; query.cursor="1";
    query.filters={{"playlistId",QString("p1")}};
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(query); QTRY_COMPARE(pages.size(),1);
    const auto section=qvariant_cast<PageResultV2>(pages[0][1]).sections[0];
    QCOMPARE(section.items.size(),1);
    QCOMPARE(section.items[0].metadata.value("playlistId"),QVariant(QString("p1")));
    QCOMPARE(section.items[0].metadata.value("playlistIndex").metaType().id(),QMetaType::Int);
    QCOMPARE(section.items[0].metadata.value("playlistIndex").toInt(),1);
    QVERIFY(section.hasMore); QCOMPARE(section.nextCursor,QString("2"));
}

void NavidromeSourceTest::streamDescriptorUsesAuthenticatedUrlWithoutPersistingIt()
{
    // Returning a stream URL through an action/page payload makes credentials cacheable.
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(8533),&network);
    QSignalSpy started(&session,&IMusicSourceSessionV2::requestStarted);
    QSignalSpy streams(&session,&IMusicSourceSessionV2::streamReady);
    const auto id=qobject_cast<IPlaybackProviderV2 *>(&session)->resolveStream(media());
    QCOMPARE(started.size(),1); QCOMPARE(started[0][0].toUuid(),id);
    QTRY_COMPARE(streams.size(),1);
    const auto stream=qvariant_cast<StreamDescriptorV2>(streams[0][1]);
    QVERIFY(stream.url.path().endsWith("/rest/stream.view"));
    QCOMPARE(QUrlQuery(stream.url).queryItemValue("id"),QString("42"));
    QCOMPARE(stream.media,media());
}

void NavidromeSourceTest::artworkResolvesStableServerCoverIdsAndAdvertisesOnlySupportedEntities()
{
    // Using entity IDs as cover IDs fetches the wrong object; unsupported mapped actions always fail.
    const SourceIdentityV2 source{QStringLiteral("navidrome"),
                                  QStringLiteral("navidrome/admin"),
                                  QStringLiteral("admin"), QStringLiteral("Navidrome Admin")};
    const MediaItemV2 track = NavidromeMappers::song(
        QJsonObject{{"id","track-entity"},{"title","Track"},{"coverArt","track-cover"}}, source);
    const MediaItemV2 album = NavidromeMappers::album(
        QJsonObject{{"id","album-entity"},{"name","Album"},{"coverArt","album-cover"}}, source);
    const MediaItemV2 artist = NavidromeMappers::artist(
        QJsonObject{{"id","artist-entity"},{"name","Artist"},{"coverArt","artist-cover"}}, source);
    const MediaItemV2 playlist = NavidromeMappers::playlist(
        QJsonObject{{"id","playlist-entity"},{"name","Playlist"},{"coverArt","playlist-cover"}}, source);
    const MediaItemV2 trackWithoutCover = NavidromeMappers::song(
        QJsonObject{{"id","no-track-cover"},{"title","No cover"}},source);
    const MediaItemV2 albumWithoutCover = NavidromeMappers::album(
        QJsonObject{{"id","no-album-cover"},{"name","No cover"}},source);
    QVERIFY(track.availableActions.contains(SourceActionV2::Artwork));
    QVERIFY(album.availableActions.contains(SourceActionV2::Artwork));
    QVERIFY(!artist.availableActions.contains(SourceActionV2::Artwork));
    QVERIFY(!playlist.availableActions.contains(SourceActionV2::Artwork));
    QVERIFY(!trackWithoutCover.availableActions.contains(SourceActionV2::Artwork));
    QVERIFY(!albumWithoutCover.availableActions.contains(SourceActionV2::Artwork));

    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueue(subsonicOk({{"song",QJsonObject{{"id","track-entity"},
                                                    {"coverArt","track-cover"}}}}));
    server.enqueueBytes("track-image", "image/jpeg");
    server.enqueue(subsonicOk({{"album",QJsonObject{{"id","album-entity"},
                                                     {"coverArt","album-cover"}}}}));
    server.enqueueBytes("album-image", "image/png");
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()), &network);
    QSignalSpy completed(&session, &IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
    auto *provider = qobject_cast<IPlaybackProviderV2 *>(&session);

    provider->fetchArtwork(track.ref);
    QTRY_COMPARE(completed.size(), 1);
    provider->fetchArtwork(album.ref);
    QTRY_COMPARE(completed.size(), 2);
    QCOMPARE(server.requests().size(), 4);
    QCOMPARE(server.requests().at(0).url.path(), QStringLiteral("/rest/getSong.view"));
    QCOMPARE(QUrlQuery(server.requests().at(0).url).queryItemValue("id"), QStringLiteral("track-entity"));
    QCOMPARE(server.requests().at(1).url.path(), QStringLiteral("/rest/getCoverArt.view"));
    QCOMPARE(QUrlQuery(server.requests().at(1).url).queryItemValue("id"), QStringLiteral("track-cover"));
    QCOMPARE(server.requests().at(2).url.path(), QStringLiteral("/rest/getAlbum.view"));
    QCOMPARE(QUrlQuery(server.requests().at(2).url).queryItemValue("id"), QStringLiteral("album-entity"));
    QCOMPARE(server.requests().at(3).url.path(), QStringLiteral("/rest/getCoverArt.view"));
    QCOMPARE(QUrlQuery(server.requests().at(3).url).queryItemValue("id"), QStringLiteral("album-cover"));

    provider->fetchArtwork(artist.ref);
    provider->fetchArtwork(playlist.ref);
    QTRY_COMPARE(failed.size(), 2);
    QCOMPARE(server.requests().size(), 4);
}

void NavidromeSourceTest::artworkPayloadIsBoundedAndTyped()
{
    // Unbounded readAll permits an oversized cover to exhaust host memory.
    FakeNavidromeServer server; QVERIFY(server.start());
    server.enqueue(subsonicOk({{"song",QJsonObject{{"id","42"},{"coverArt","cover-42"}}}}));
    server.enqueueBytes("image-bytes","image/jpeg");
    server.enqueue(subsonicOk({{"song",QJsonObject{{"id","43"},{"coverArt","cover-43"}}}}));
    server.enqueueBytes("x","image/jpeg",32ll*1024*1024+1);
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    auto *provider=qobject_cast<IPlaybackProviderV2 *>(&session);
    provider->fetchArtwork(media()); QTRY_COMPARE(completed.size(),1);
    const auto result=qvariant_cast<ActionResultV2>(completed[0][1]);
    QCOMPARE(result.action,SourceActionV2::Artwork);
    QCOMPARE(result.payload.value("bytes").metaType().id(),QMetaType::QByteArray);
    QCOMPARE(result.payload.value("bytes").toByteArray(),QByteArray("image-bytes"));
    QCOMPARE(result.payload.value("mimeType"),QVariant(QString("image/jpeg")));
    QVERIFY(!result.payload.contains("url"));
    provider->fetchArtwork(media("43")); QTRY_COMPARE(failed.size(),1);
    const auto error=qvariant_cast<SourceErrorV2>(failed[0][1]);
    QCOMPARE(error.kind,SourceErrorKindV2::Unavailable); QVERIFY(!error.retryable);
    QVERIFY(!error.detail.contains("http")); QVERIFY(!error.detail.contains("token"));
}

void NavidromeSourceTest::artworkBoundaryAcceptsExactAndRejectsAccumulatedOverflow()
{
    // Off-by-one or declared-length-only guards mishandle the exact limit or streamed overflow.
    constexpr qsizetype maximum=32*1024*1024;
    {
        FakeNavidromeServer server;
        QVERIFY(server.start());
        server.enqueue(subsonicOk({{"song",QJsonObject{{"id","exact"},{"coverArt","exact-cover"}}}}));
        server.enqueueBytes(QByteArray(maximum,'x'),"image/jpeg");
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()),&network);
        QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
        QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
        qobject_cast<IPlaybackProviderV2 *>(&session)->fetchArtwork(media("exact"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,30000);
        QCOMPARE(failed.size(),0);
        QCOMPARE(qvariant_cast<ActionResultV2>(completed.constFirst().at(1))
                     .payload.value("bytes").toByteArray().size(),maximum);
    }
    {
        FakeNavidromeServer server;
        QVERIFY(server.start());
        server.enqueue(subsonicOk({{"song",QJsonObject{{"id","overflow"},{"coverArt","overflow-cover"}}}}));
        server.enqueueBytesWithoutLength(QByteArray(maximum+1,'y'),"image/jpeg");
        QNetworkAccessManager network;
        NavidromeSourceSession session(configuration(server.serverPort()),&network);
        QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
        QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
        qobject_cast<IPlaybackProviderV2 *>(&session)->fetchArtwork(media("overflow"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,30000);
        QCOMPARE(completed.size(),0);
        const SourceErrorV2 error=qvariant_cast<SourceErrorV2>(failed.constFirst().at(1));
        QCOMPARE(error.kind,SourceErrorKindV2::Unavailable);
        QVERIFY(!error.retryable);
    }
}

void NavidromeSourceTest::successfulMediaReadsAreBounded_data()
{
    QTest::addColumn<bool>("download");
    QTest::newRow("binary-oversized-single-chunk") << false;
    QTest::newRow("download-single-chunk") << true;
}

void NavidromeSourceTest::successfulMediaReadsAreBounded()
{
    // A successful reply with one large available chunk made readAll allocate the whole chunk.
    QFETCH(bool,download);
    constexpr qint64 maximumArtworkBytes=32ll*1024*1024;
    constexpr qint64 expectedReadBound=64ll*1024;
    const QByteArray body(download ? 3*expectedReadBound+17
                                   : maximumArtworkBytes+expectedReadBound,'m');
    ReadObservation observation;
    SingleAvailableChunkNetwork network(body,download ? QStringLiteral("audio/mpeg")
                                                       : QStringLiteral("image/jpeg"),
                                        &observation);
    NavidromeApiClient client(configuration(8533),&network,
                              [] { return QStringLiteral("bounded-read-salt"); });
    QSignalSpy binaryDone(&client,&NavidromeApiClient::binarySucceeded);
    QSignalSpy downloadDone(&client,&NavidromeApiClient::downloadSucceeded);
    QSignalSpy failed(&client,&NavidromeApiClient::failed);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination=dir.filePath(QStringLiteral("media.bin"));
    if (download)
        client.downloadToFile(QStringLiteral("download"),QStringLiteral("download"),{},destination);
    else
        client.getBinary(QStringLiteral("artwork"),QStringLiteral("getCoverArt"),{},
                         maximumArtworkBytes);

    if (download) {
        QTRY_COMPARE(downloadDone.size(),1);
        QCOMPARE(failed.size(),0);
        QFile file(destination);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),body);
        QCOMPARE(observation.bytesRead,qint64(body.size()));
    } else {
        QTRY_COMPARE(failed.size(),1);
        QCOMPARE(binaryDone.size(),0);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed.constFirst().at(1)).messageKey,
                 QStringLiteral("source.artwork.tooLarge"));
        QCOMPARE(observation.bytesRead,maximumArtworkBytes+1);
    }
    QVERIFY(observation.requestedSizes.size()>1);
    for (qint64 requested:observation.requestedSizes)
        QVERIFY2(requested<=expectedReadBound,qPrintable(QStringLiteral("unbounded read request: %1")
                                                              .arg(requested)));
    QCOMPARE(observation.configuredReadBufferSize,expectedReadBound);
}

void NavidromeSourceTest::lyricsUsesNegotiatedEndpoint()
{
    // Ignoring the negotiated songLyrics extension forces a lossy artist/title fallback.
    FakeNavidromeServer server; QVERIFY(server.start()); enqueueSuccessfulHandshake(server);
    server.enqueue(subsonicOk({{"lyricsList",QJsonObject{{"structuredLyrics",QJsonArray{
        QJsonObject{{"line",QJsonArray{QJsonObject{{"start",1000},{"value","line one"}},
                                                   QJsonObject{{"start",2000},{"value","line two"}}}}}}}}}}));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    session.open(); QTRY_COMPARE(session.state(),SourceSessionStateV2::Ready);
    QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
    qobject_cast<IPlaybackProviderV2 *>(&session)->fetchLyrics(media());
    QTRY_COMPARE(completed.size(),1);
    const auto result=qvariant_cast<ActionResultV2>(completed[0][1]);
    QCOMPARE(result.action,SourceActionV2::Lyrics);
    QCOMPARE(result.payload.value("lyrics"),QVariant(QString("line one\nline two")));
    QCOMPARE(server.requests().last().url.path(),QString("/rest/getLyricsBySongId.view"));
}

void NavidromeSourceTest::downloadRejectsInvalidDestinationsBeforeNetwork()
{
    // Deferring destination validation can leak credentials on requests that can never commit.
    FakeNavidromeServer server; QVERIFY(server.start()); QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString existingPath=dir.filePath("existing.mp3"); QFile existing(existingPath);
    QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("keep"); existing.close();
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    auto *provider=qobject_cast<IDownloadProviderV2 *>(&session);
    const QList<QUrl> invalid{QUrl("https://example.invalid/song.mp3"),QUrl("relative.mp3"),
                              QUrl::fromLocalFile(existingPath),
                              QUrl::fromLocalFile(dir.filePath("missing/song.mp3"))};
    for (const auto &destination:invalid) provider->download(media(),destination);
    QTRY_COMPARE(failed.size(),invalid.size()); QCOMPARE(server.requests().size(),0);
    for (const auto &args:failed)
        QCOMPARE(qvariant_cast<SourceErrorV2>(args[1]).kind,SourceErrorKindV2::InvalidRequest);
    QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(),QByteArray("keep"));
}

void NavidromeSourceTest::downloadRejectsDanglingSymlinkBeforeNetwork()
{
    // QFileInfo::exists follows a dangling link, allowing an occupied path to reach transport.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueueBytes("must-not-download");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing=dir.filePath(QStringLiteral("missing-target"));
    const QString destination=dir.filePath(QStringLiteral("dangling.mp3"));
    QVERIFY(QFile::link(missing,destination));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(QFileInfo(destination).isSymLink());
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    qobject_cast<IDownloadProviderV2 *>(&session)->download(
        media(),QUrl::fromLocalFile(destination));
    QTRY_COMPARE(failed.size(),1);
    QCOMPARE(qvariant_cast<SourceErrorV2>(failed.constFirst().at(1)).kind,
             SourceErrorKindV2::InvalidRequest);
    QCOMPARE(server.requests().size(),0);
    QVERIFY(QFileInfo(destination).isSymLink());
    QCOMPARE(QFileInfo(destination).symLinkTarget(),missing);
}

void NavidromeSourceTest::downloadWritesRequestedLocalFileAtomically()
{
    // Emitting completion before rename exposes a path that does not contain the requested bytes.
    FakeNavidromeServer server; QVERIFY(server.start()); server.enqueueBytes("audio-bytes","audio/mpeg");
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QUrl destination=QUrl::fromLocalFile(dir.filePath("song.mp3"));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted); bool existedAtSignal=false;
    connect(&session,&IMusicSourceSessionV2::actionCompleted,&session,[&](QUuid,const ActionResultV2 &) {
        existedAtSignal=QFileInfo::exists(destination.toLocalFile());
    });
    const auto id=qobject_cast<IDownloadProviderV2 *>(&session)->download(media(),destination);
    QTRY_COMPARE(completed.size(),1); QCOMPARE(completed[0][0].toUuid(),id); QVERIFY(existedAtSignal);
    const auto result=qvariant_cast<ActionResultV2>(completed[0][1]);
    QCOMPARE(result.action,SourceActionV2::Download);
    QCOMPARE(result.payload,QVariantMap({{"destination",destination}}));
    QFile file(destination.toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(),QByteArray("audio-bytes"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(),1);
}

void NavidromeSourceTest::downloadRenameRaceAndCancellationLeaveNoPartialFiles()
{
    // Overwriting a raced destination or retaining temp files violates atomic no-overwrite semantics.
    FakeNavidromeServer server; QVERIFY(server.start()); server.enqueueBytes("incoming"); server.enqueueHeld();
    QTemporaryDir dir; QVERIFY(dir.isValid()); QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    auto *provider=qobject_cast<IDownloadProviderV2 *>(&session);
    const QString racedPath=dir.filePath("raced.mp3");
    provider->download(media(),QUrl::fromLocalFile(racedPath));
    QFile raced(racedPath); QVERIFY(raced.open(QIODevice::WriteOnly)); raced.write("winner"); raced.close();
    QTRY_COMPARE(failed.size(),1); QCOMPARE(completed.size(),0);
    QVERIFY(raced.open(QIODevice::ReadOnly)); QCOMPARE(raced.readAll(),QByteArray("winner")); raced.close();
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files),QStringList({"raced.mp3"}));
    const QString cancelledPath=dir.filePath("cancelled.mp3");
    const auto cancelled=provider->download(media("44"),QUrl::fromLocalFile(cancelledPath));
    QTRY_COMPARE(server.requests().size(),2); session.cancel(cancelled); QTest::qWait(20);
    QCOMPARE(completed.size(),0); QVERIFY(!QFileInfo::exists(cancelledPath));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files),QStringList({"raced.mp3"}));
}

void NavidromeSourceTest::binaryMediaRejectsSubsonicErrorBodiesAndCleansUp()
{
    // Treating a 200 JSON error as media publishes error text as artwork or a song file.
    FakeNavidromeServer server; QVERIFY(server.start());
    server.enqueue(subsonicOk({{"song",QJsonObject{{"id","42"},{"coverArt","cover-42"}}}}));
    server.enqueue(subsonicError(50,QStringLiteral("Denied")));
    server.enqueue(subsonicError(50,QStringLiteral("Denied")));
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QUrl destination=QUrl::fromLocalFile(dir.filePath("song.mp3"));
    QNetworkAccessManager network; NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    qobject_cast<IPlaybackProviderV2 *>(&session)->fetchArtwork(media());
    QTRY_COMPARE(failed.size(),1);
    qobject_cast<IDownloadProviderV2 *>(&session)->download(media(),destination);
    QTRY_COMPARE(failed.size(),2);
    QCOMPARE(completed.size(),0); QVERIFY(!QFileInfo::exists(destination.toLocalFile()));
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    for (const auto &args:failed)
        QCOMPARE(qvariant_cast<SourceErrorV2>(args[1]).kind,SourceErrorKindV2::Authorization);
}

void NavidromeSourceTest::mediaResponsePrecedence_data()
{
    QTest::addColumn<bool>("download");
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("subsonicCode");
    QTest::addColumn<SourceErrorKindV2>("expectedKind");
    const QList<QPair<int,SourceErrorKindV2>> statuses{
        {401,SourceErrorKindV2::Authentication}, {403,SourceErrorKindV2::Authorization},
        {404,SourceErrorKindV2::Unsupported}, {405,SourceErrorKindV2::Unsupported},
        {500,SourceErrorKindV2::Network}, {501,SourceErrorKindV2::Unsupported}};
    for (bool download : {false,true}) {
        QTest::addRow("%s-subsonic", download ? "download" : "binary")
            << download << 200 << 50 << SourceErrorKindV2::Authorization;
        for (const auto &[status,kind] : statuses) {
            const int conflictingCode = status == 403 ? 40 : 50;
            QTest::addRow("%s-http-%d", download ? "download" : "binary", status)
                << download << status << conflictingCode << kind;
        }
    }
}

void NavidromeSourceTest::mediaResponsePrecedence()
{
    // Artwork-size or Subsonic parsing must not override authoritative HTTP failures.
    QFETCH(bool, download);
    QFETCH(int, status);
    QFETCH(int, subsonicCode);
    QFETCH(SourceErrorKindV2, expectedKind);
    FakeNavidromeServer server;
    QVERIFY(server.start());
    const QByteArray body = QJsonDocument(subsonicError(subsonicCode,QStringLiteral("conflict")))
                                .toJson(QJsonDocument::Compact);
    server.enqueueBytes(body, "application/json", -1, status);
    QNetworkAccessManager network;
    NavidromeApiClient client(configuration(server.serverPort()), &network,
                              [] { return QStringLiteral("precedence-salt"); });
    QSignalSpy binaryDone(&client, &NavidromeApiClient::binarySucceeded);
    QSignalSpy downloadDone(&client, &NavidromeApiClient::downloadSucceeded);
    QSignalSpy failed(&client, &NavidromeApiClient::failed);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination = dir.filePath(QStringLiteral("media.bin"));
    if (download)
        client.downloadToFile(QStringLiteral("download"),QStringLiteral("download"),{},destination);
    else
        client.getBinary(QStringLiteral("artwork"),QStringLiteral("getCoverArt"),{},1);
    QTRY_COMPARE(failed.size(),1);
    const SourceErrorV2 failure=qvariant_cast<SourceErrorV2>(failed.constFirst().at(1));
    QCOMPARE(failure.kind,expectedKind);
    QCOMPARE(failure.httpStatus,std::optional<int>(status));
    QCOMPARE(binaryDone.size(),0);
    QCOMPARE(downloadDone.size(),0);
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void NavidromeSourceTest::downloadFinalFlushFailureRemovesArtifacts()
{
    // Ignoring a failed final flush can publish a truncated destination as success.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueueBytes("audio-bytes", "audio/mpeg");
    QNetworkAccessManager network;
    NavidromeApiClient client(configuration(server.serverPort()), &network,
                              [] { return QStringLiteral("flush-salt"); },
                              [](QTemporaryFile &) { return false; });
    QSignalSpy completed(&client, &NavidromeApiClient::downloadSucceeded);
    QSignalSpy failed(&client, &NavidromeApiClient::failed);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination=dir.filePath(QStringLiteral("song.mp3"));
    client.downloadToFile(QStringLiteral("download"),QStringLiteral("download"),{},destination);
    QTRY_COMPARE(failed.size(),1);
    const SourceErrorV2 failure=qvariant_cast<SourceErrorV2>(failed.constFirst().at(1));
    QCOMPARE(failure.kind,SourceErrorKindV2::Unavailable);
    QCOMPARE(failure.messageKey,QStringLiteral("source.download.ioFailed"));
    QCOMPARE(completed.size(),0);
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void NavidromeSourceTest::downloadWriteFailureRemovesArtifacts()
{
    // Accepting a short temporary-file write can publish incomplete download data.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueueBytes("audio-bytes", "audio/mpeg");
    QNetworkAccessManager network;
    NavidromeApiClient client(
        configuration(server.serverPort()), &network,
        [] { return QStringLiteral("write-salt"); },
        [](QTemporaryFile &,const QByteArray &) { return qint64(-1); },
        [](QTemporaryFile &file) { return file.flush(); });
    QSignalSpy completed(&client,&NavidromeApiClient::downloadSucceeded);
    QSignalSpy failed(&client,&NavidromeApiClient::failed);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination=dir.filePath(QStringLiteral("song.mp3"));
    client.downloadToFile(QStringLiteral("download"),QStringLiteral("download"),{},destination);
    QTRY_COMPARE(failed.size(),1);
    const SourceErrorV2 failure=qvariant_cast<SourceErrorV2>(failed.constFirst().at(1));
    QCOMPARE(failure.kind,SourceErrorKindV2::Unavailable);
    QCOMPARE(failure.messageKey,QStringLiteral("source.download.ioFailed"));
    QCOMPARE(completed.size(),0);
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void NavidromeSourceTest::downloadNetworkFailureRemovesArtifacts()
{
    // A connection failure after temp creation must not leave a destination or partial file.
    FakeNavidromeServer unavailable;
    QVERIFY(unavailable.start());
    const quint16 port=unavailable.serverPort();
    unavailable.close();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination=dir.filePath(QStringLiteral("song.mp3"));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(port),&network);
    QSignalSpy completed(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    qobject_cast<IDownloadProviderV2 *>(&session)->download(
        media(),QUrl::fromLocalFile(destination));
    QTRY_COMPARE(failed.size(),1);
    QCOMPARE(qvariant_cast<SourceErrorV2>(failed.constFirst().at(1)).kind,
             SourceErrorKindV2::Network);
    QCOMPARE(completed.size(),0);
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void NavidromeSourceTest::task10RequestStartedReentrancy_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<bool>("closeSession");
    for (bool closeSession:{false,true})
        for (int operation=0;operation<5;++operation)
            QTest::addRow("%s-%d",closeSession?"close":"cancel",operation)
                << operation << closeSession;
}

void NavidromeSourceTest::task10RequestStartedReentrancy()
{
    // Dispatch after reentrant cancel/close can create an uncorrelated request and late terminal.
    QFETCH(int,operation);
    QFETCH(bool,closeSession);
    FakeNavidromeServer server;
    QVERIFY(server.start());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    QSignalSpy streams(&session,&IMusicSourceSessionV2::streamReady);
    QSignalSpy actions(&session,&IMusicSourceSessionV2::actionCompleted);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    QUuid synchronousId;
    connect(&session,&IMusicSourceSessionV2::requestStarted,&session,[&](const QUuid &id) {
        synchronousId=id;
        if (closeSession) session.close(); else session.cancel(id);
    });
    QUuid returned;
    switch (operation) {
    case 0:
        returned=qobject_cast<IPageProviderV2 *>(&session)->fetchPage(
            pageQuery(PageSectionKindV2::Random));
        break;
    case 1:
        returned=qobject_cast<IPlaybackProviderV2 *>(&session)->resolveStream(media());
        break;
    case 2:
        returned=qobject_cast<IPlaybackProviderV2 *>(&session)->fetchArtwork(media());
        break;
    case 3:
        returned=qobject_cast<IPlaybackProviderV2 *>(&session)->fetchLyrics(media());
        break;
    default:
        returned=qobject_cast<IDownloadProviderV2 *>(&session)->download(
            media(),QUrl::fromLocalFile(dir.filePath(QStringLiteral("song.mp3"))));
        break;
    }
    QCOMPARE(synchronousId,returned);
    QTest::qWait(30);
    QCOMPARE(pages.size(),0);
    QCOMPARE(streams.size(),0);
    QCOMPARE(actions.size(),0);
    QCOMPARE(failed.size(),0);
    QCOMPARE(server.requests().size(),0);
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void NavidromeSourceTest::cancelledPageDropsLateServerResponse()
{
    // A server response arriving after cancellation must not recover stale public correlation.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueueHeld(QJsonDocument(subsonicOk({{"albumList2",QJsonObject{{"album",QJsonArray{
        QJsonObject{{"id","late"},{"name","Late"}}}}}}})).toJson(QJsonDocument::Compact),
                       "application/json");
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network);
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    QSignalSpy failed(&session,&IMusicSourceSessionV2::requestFailed);
    const QUuid id=qobject_cast<IPageProviderV2 *>(&session)->fetchPage(
        pageQuery(PageSectionKindV2::Random));
    QTRY_COMPARE(server.requests().size(),1);
    session.cancel(id);
    server.releaseHeld();
    QTest::qWait(50);
    QCOMPARE(pages.size(),0);
    QCOMPARE(failed.size(),0);
}

void NavidromeSourceTest::artworkSecondStageTeardownDropsLateResponse_data()
{
    QTest::addColumn<int>("teardown");
    QTest::newRow("cancel") << 0;
    QTest::newRow("close") << 1;
    QTest::newRow("destroy") << 2;
}

void NavidromeSourceTest::artworkSecondStageTeardownDropsLateResponse()
{
    // Losing correlation only after getCoverArt starts can emit a late artwork terminal.
    QFETCH(int,teardown);
    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueue(subsonicOk({{"song",QJsonObject{{"id","42"},{"coverArt","held-cover"}}}}));
    server.enqueueHeld("late-image","image/jpeg");
    QNetworkAccessManager network;
    auto session=std::make_unique<NavidromeSourceSession>(
        configuration(server.serverPort()),&network);
    int completed=0;
    int failed=0;
    connect(session.get(),&IMusicSourceSessionV2::actionCompleted,this,
            [&completed] { ++completed; });
    connect(session.get(),&IMusicSourceSessionV2::requestFailed,this,
            [&failed] { ++failed; });
    const QUuid id=qobject_cast<IPlaybackProviderV2 *>(session.get())->fetchArtwork(media());
    QTRY_COMPARE(server.requests().size(),2);
    QVERIFY(server.requests().constLast().url.path().endsWith(QStringLiteral("/getCoverArt.view")));
    QCOMPARE(QUrlQuery(server.requests().constLast().url).queryItemValue(QStringLiteral("id")),
             QStringLiteral("held-cover"));
    if (teardown==0) session->cancel(id);
    else if (teardown==1) session->close();
    else session.reset();
    server.releaseHeld();
    QTest::qWait(50);
    QCOMPARE(completed,0);
    QCOMPARE(failed,0);
}

void NavidromeSourceTest::closeAndDestructionCleanDownloadTemporaryFiles()
{
    // Close or destruction with an active transfer must remove the same-directory temp file.
    for (bool destroy:{false,true}) {
        FakeNavidromeServer server;
        QVERIFY(server.start());
        server.enqueueHeld();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QNetworkAccessManager network;
        auto session=std::make_unique<NavidromeSourceSession>(
            configuration(server.serverPort()),&network);
        QSignalSpy completed(session.get(),&IMusicSourceSessionV2::actionCompleted);
        QSignalSpy failed(session.get(),&IMusicSourceSessionV2::requestFailed);
        qobject_cast<IDownloadProviderV2 *>(session.get())->download(
            media(),QUrl::fromLocalFile(dir.filePath(QStringLiteral("song.mp3"))));
        QTRY_COMPARE(server.requests().size(),1);
        QTRY_COMPARE(QDir(dir.path()).entryList(QDir::Files).size(),1);
        if (destroy) session.reset(); else session->close();
        QTRY_VERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
        if (!destroy) {
            QCOMPARE(completed.size(),0);
            QCOMPARE(failed.size(),0);
        }
    }
}

void NavidromeSourceTest::pageDtoDoesNotLeakAuthenticatedRequestData()
{
    // Scanning selected live fields misses credentials in other DTO fields or disk hydration.
    FakeNavidromeServer server;
    QVERIFY(server.start());
    server.enqueue(subsonicOk({{"albumList2",QJsonObject{{"album",QJsonArray{QJsonObject{
        {"id","album-1"},{"name","Album"},{"coverArt","cover-1"},
        {"streamUrl","http://private.invalid/?u=admin&t=forbidden&s=leak-salt"}}}}}}}));
    QNetworkAccessManager network;
    NavidromeSourceSession session(configuration(server.serverPort()),&network,
                                    [] { return QStringLiteral("leak-salt"); });
    QSignalSpy pages(&session,&IMusicSourceSessionV2::pageReady);
    qobject_cast<IPageProviderV2 *>(&session)->fetchPage(pageQuery(PageSectionKindV2::Random));
    QTRY_COMPARE(pages.size(),1);
    const PageResultV2 page=qvariant_cast<PageResultV2>(pages.constFirst().at(1));
    QCOMPARE(page.sections.size(),1);
    QCOMPARE(page.sections.constFirst().items.size(),1);
    const QByteArray token=QCryptographicHash::hash(
        QByteArrayLiteral("test-password")+QByteArrayLiteral("leak-salt"),QCryptographicHash::Md5).toHex();
    const QList<QByteArray> forbidden{QByteArrayLiteral("leak-salt"),token,
                                      QByteArrayLiteral("streamUrl"),
                                      QByteArrayLiteral("private.invalid"),
                                      QByteArrayLiteral("test-password"),
                                      QByteArrayLiteral("Authorization")};
    QVERIFY(!variantContains(pageDto(page),forbidden));

    QTemporaryDir cacheDirectory;
    QVERIFY(cacheDirectory.isValid());
    PageCacheKeyV2 key;
    key.query=pageQuery(PageSectionKindV2::Random);
    key.sourceInstanceIds={QStringLiteral("navidrome/admin")};
    const QDateTime storedAt=QDateTime::currentDateTimeUtc();
    QString cachePath;
    {
        PageCache writer(cacheDirectory.path());
        QVERIFY(writer.store(key,page,storedAt));
        cachePath=writer.filePath(key);
    }
    QFile cacheFile(cachePath);
    QVERIFY(cacheFile.open(QIODevice::ReadOnly));
    const QJsonDocument cacheDocument=QJsonDocument::fromJson(cacheFile.readAll());
    QVERIFY(cacheDocument.isObject());
    QVERIFY(!variantContains(cacheDocument.toVariant(),forbidden));
    cacheFile.close();

    PageCache reader(cacheDirectory.path());
    const auto cached=reader.lookup(key,storedAt,std::chrono::minutes(5));
    QVERIFY(cached);
    QVERIFY(cached->cached);
    QVERIFY(cached->page.cached);
    QVERIFY(!cached->page.complete);
    PageResultV2 expected=PageCache::sanitized(page);
    expected.cached=true;
    expected.complete=false;
    QCOMPARE(pageDto(cached->page),pageDto(expected));
    QVERIFY(!variantContains(pageDto(cached->page),forbidden));
}

QTEST_MAIN(NavidromeSourceTest)
#include "tst_NavidromeSource.moc"
