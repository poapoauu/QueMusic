#include "MediaBridge.h"
#include "IMusicSourceSession.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceManager.h"

#define private public
#include "SourceSessionRegistry.h"
#undef private

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

namespace {

SourceAccount navidromeAccount(quint16 port)
{
    return {QStringLiteral("navidrome"),
            QStringLiteral("home"),
            QStringLiteral("Home server"),
            {{QStringLiteral("serverUrl"), QStringLiteral("http://127.0.0.1:%1").arg(port)},
             {QStringLiteral("username"), QStringLiteral("home-user")}},
            QByteArrayLiteral("test-password")};
}

void sendJsonResponse(QTcpSocket *socket, int status, const QByteArray &response)
{
    const QByteArray reason = status >= 200 && status < 300 ? QByteArrayLiteral("OK")
                                                         : QByteArrayLiteral("Service Unavailable");
    socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + reason
                  + "\r\nContent-Type: application/json\r\nContent-Length: "
                  + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n"
                  + response);
    socket->disconnectFromHost();
}

class MemorySecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret, QString *error) override
    {
        Q_UNUSED(error)
        values.insert(reference, secret);
        return true;
    }

    std::optional<QByteArray> read(const QString &reference, QString *error) const override
    {
        const auto value = values.constFind(reference);
        if (value == values.cend()) {
            if (error) {
                *error = QStringLiteral("Secret is unavailable");
            }
            return std::nullopt;
        }
        return *value;
    }

    bool remove(const QString &reference, QString *error) override
    {
        Q_UNUSED(error)
        values.remove(reference);
        return true;
    }

private:
    QHash<QString, QByteArray> values;
};

SourceAccount testSourceAccount()
{
    return {QStringLiteral("test-source"),
            QStringLiteral("home"),
            QStringLiteral("Test account"),
            {{QStringLiteral("serverUrl"), QStringLiteral("https://music.example.invalid")},
             {QStringLiteral("username"), QStringLiteral("test-user")}},
            QByteArrayLiteral("test-secret")};
}

class RegistryHarness {
public:
    RegistryHarness()
        : settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")), QSettings::IniFormat)
        , accountStore(&settings, &secretStore)
        , sourceManager(&pluginManager)
        , registry(&sourceManager, &accountStore)
    {
        pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    }

    bool initialize()
    {
        return temporaryDirectory.isValid() && sourceManager.loadAll() == 1
            && accountStore.upsert(testSourceAccount());
    }

    QTemporaryDir temporaryDirectory;
    QSettings settings;
    MemorySecretStore secretStore;
    SourceAccountStore accountStore;
    PluginManager pluginManager;
    SourceManager sourceManager;
    SourceSessionRegistry registry;
};

class NavidromeFixture {
public:
    NavidromeFixture()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                const QByteArray request = socket->readAll();
                if (responses.isEmpty()) {
                    return;
                }
                requestLines.append(request.split('\n').constFirst().trimmed());
                const int status = statuses.isEmpty() ? 200 : statuses.takeFirst();
                sendJsonResponse(socket, status, responses.takeFirst());
            });
        });
    }

    void start()
    {
        QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    }

    QTcpServer server;
    QList<QByteArray> responses;
    QList<int> statuses;
    QList<QByteArray> requestLines;
};

class NavidromeMappingSession final : public IMusicSourceSession {
public:
    NavidromeMappingSession(SourceAccount account, QNetworkAccessManager *network,
                             QObject *parent = nullptr)
        : IMusicSourceSession(parent), m_account(std::move(account)), m_network(network)
    {
    }

    QUuid search(const SearchQuery &query) override
    {
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("query"), query.query);
        parameters.addQueryItem(QStringLiteral("songCount"), QString::number(query.limit));
        parameters.addQueryItem(QStringLiteral("albumCount"), QString::number(query.limit));
        parameters.addQueryItem(QStringLiteral("artistCount"), QString::number(query.limit));
        return start(QStringLiteral("search"), QStringLiteral("search3"), parameters);
    }

    QUuid browse(const BrowseQuery &query) override
    {
        QUrlQuery parameters;
        if (query.path.isEmpty())
            return start(QStringLiteral("browse"), QStringLiteral("getIndexes"), parameters);
        parameters.addQueryItem(QStringLiteral("id"), query.path);
        return start(QStringLiteral("browse"), QStringLiteral("getMusicDirectory"), parameters);
    }

    QUuid resolveStream(const TrackRef &) override { return {}; }
    QUuid fetchArtwork(const TrackRef &) override { return {}; }
    QUuid fetchLyrics(const TrackRef &) override { return {}; }

    void cancel(const QUuid &requestId) override
    {
        const QPointer<QNetworkReply> reply = m_pending.take(requestId);
        if (reply) {
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
        }
    }

private:
    QUuid start(const QString &operation, const QString &endpoint, QUrlQuery query)
    {
        const QUuid requestId = QUuid::createUuid();
        QUrl url(m_account.parameters.value(QStringLiteral("serverUrl")).toString());
        url.setPath(QStringLiteral("/rest/%1.view").arg(endpoint));
        url.setQuery(query);
        QNetworkReply *reply = m_network->get(QNetworkRequest(url));
        m_pending.insert(requestId, reply);
        connect(reply, &QNetworkReply::finished, this,
                [this, requestId, operation, endpoint, reply] {
            if (m_pending.take(requestId) != reply)
                return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray payload = reply->readAll();
            const QNetworkReply::NetworkError networkError = reply->error();
            reply->deleteLater();
            if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
                emit requestFailed(requestId,
                                   {SourceErrorKind::Network,
                                    QStringLiteral("Fixture request failed"), status});
                return;
            }
            const QJsonObject response = QJsonDocument::fromJson(payload).object()
                                             .value(QStringLiteral("subsonic-response"))
                                             .toObject();
            if (response.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
                emit requestFailed(requestId,
                                   {SourceErrorKind::InvalidRequest,
                                    QStringLiteral("Fixture response is invalid"), status});
                return;
            }
            QJsonArray items;
            if (operation == QStringLiteral("search")) {
                const QJsonObject result = response.value(QStringLiteral("searchResult3")).toObject();
                const auto append = [&items](const QJsonArray &values, const QString &kind) {
                    for (const QJsonValue &value : values) {
                        const QJsonObject item = value.toObject();
                        QJsonObject mapped{{QStringLiteral("kind"), kind},
                                           {QStringLiteral("id"), item.value(QStringLiteral("id"))},
                                           {QStringLiteral("sourceId"), QStringLiteral("navidrome")}};
                        mapped.insert(QStringLiteral("title"),
                                      kind == QStringLiteral("track")
                                          ? item.value(QStringLiteral("title"))
                                          : item.value(QStringLiteral("name")));
                        mapped.insert(QStringLiteral("artist"), item.value(QStringLiteral("artist")));
                        if (kind == QStringLiteral("track")) {
                            mapped.insert(QStringLiteral("album"), item.value(QStringLiteral("album")));
                            mapped.insert(QStringLiteral("duration"), item.value(QStringLiteral("duration")));
                            mapped.insert(QStringLiteral("coverArtId"), item.value(QStringLiteral("coverArt")));
                        }
                        items.append(mapped);
                    }
                };
                append(result.value(QStringLiteral("song")).toArray(), QStringLiteral("track"));
                append(result.value(QStringLiteral("album")).toArray(), QStringLiteral("album"));
                append(result.value(QStringLiteral("artist")).toArray(), QStringLiteral("artist"));
            } else if (endpoint == QStringLiteral("getIndexes")) {
                const QJsonObject indexes = response.value(QStringLiteral("indexes")).toObject();
                const QJsonArray artists = indexes.value(QStringLiteral("artist")).toArray();
                for (const QJsonValue &value : artists) {
                    const QJsonObject artist = value.toObject();
                    items.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("artist")},
                                             {QStringLiteral("id"), artist.value(QStringLiteral("id"))},
                                             {QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                                             {QStringLiteral("title"), artist.value(QStringLiteral("name"))}});
                }
            } else {
                const QJsonObject directory = response.value(QStringLiteral("directory")).toObject();
                for (const QJsonValue &value : directory.value(QStringLiteral("child")).toArray()) {
                    const QJsonObject child = value.toObject();
                    items.append(QJsonObject{{QStringLiteral("kind"),
                                              child.value(QStringLiteral("isDir")).toBool()
                                                  ? QStringLiteral("directory")
                                                  : QStringLiteral("track")},
                                             {QStringLiteral("id"), child.value(QStringLiteral("id"))},
                                             {QStringLiteral("sourceId"), QStringLiteral("navidrome")},
                                             {QStringLiteral("title"), child.value(QStringLiteral("title"))},
                                             {QStringLiteral("artist"), child.value(QStringLiteral("artist"))}});
                }
            }
            emit requestSucceeded(requestId, operation,
                                  QJsonObject{{QStringLiteral("items"), items}});
        });
        return requestId;
    }

    SourceAccount m_account;
    QPointer<QNetworkAccessManager> m_network;
    QHash<QUuid, QPointer<QNetworkReply>> m_pending;
};

class LateSession final : public IMusicSourceSession {
public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override { return requestId = QUuid::createUuid(); }
    QUuid browse(const BrowseQuery &) override { return requestId = QUuid::createUuid(); }
    QUuid resolveStream(const TrackRef &) override { return {}; }
    QUuid fetchArtwork(const TrackRef &) override { return {}; }
    QUuid fetchLyrics(const TrackRef &) override { return {}; }
    void cancel(const QUuid &id) override { cancelled.append(id); }

    void succeed(const QJsonValue &result)
    {
        emit requestSucceeded(requestId, QStringLiteral("search"), result);
    }

    void succeed(const QString &operation, const QJsonValue &result)
    {
        emit requestSucceeded(requestId, operation, result);
    }

    QUuid requestId;
    QList<QUuid> cancelled;
};

template<typename Session>
Session *installSession(SourceSessionRegistry *registry, const MediaId &id, Session *session)
{
    registry->m_sessions.insert(registry->keyFor(id), {session, {}});
    return session;
}

} // namespace

class MediaBridgeTest : public QObject {
    Q_OBJECT

private slots:
    void mapsNormalizedNavidromeSearchIntoFixedRoles();
    void mapsRootAndDirectoryBrowseResponses();
    void mapsEmptyAndFailureStatesAndRetriesSavedIntent();
    void ignoresTerminalCallbacksAfterCancellationOrInvalidation();
    void malformedSuccessCallbacksFailActiveModel();
};

void MediaBridgeTest::mapsNormalizedNavidromeSearchIntoFixedRoles()
{
    NavidromeFixture fixture;
    fixture.responses.append(
        "{\"subsonic-response\":{\"status\":\"ok\",\"searchResult3\":{"
        "\"song\":[{\"id\":\"song-1\",\"title\":\"Song\",\"artist\":\"Artist\","
        "\"album\":\"Album\",\"duration\":12,\"coverArt\":\"cover-1\"}],"
        "\"album\":[{\"id\":\"album-1\",\"name\":\"Album\",\"artist\":\"Artist\"}],"
        "\"artist\":[{\"id\":\"artist-1\",\"name\":\"Artist\"}]}}}");
    fixture.start();

    SourceSessionRegistry registry(nullptr, nullptr);
    QNetworkAccessManager network;
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home")};
    installSession(&registry, id, new NavidromeMappingSession(
                                      navidromeAccount(fixture.server.serverPort()), &network,
                                      &registry));
    MediaBridge bridge(&registry);

    bridge.search(QStringLiteral("navidrome/home"), QStringLiteral("Song"), 20);

    QTRY_COMPARE(bridge.searchResults()->requestState(), MediaRequestState::Ready);
    QCOMPARE(bridge.searchResults()->rowCount(), 3);
    const QVariantMap track = bridge.searchResults()->get(0);
    QCOMPARE(track.value(QStringLiteral("sourceId")).toString(), QStringLiteral("navidrome"));
    QCOMPARE(track.value(QStringLiteral("accountId")).toString(), QStringLiteral("home"));
    QCOMPARE(track.value(QStringLiteral("nativeId")).toString(), QStringLiteral("song-1"));
    QCOMPARE(track.value(QStringLiteral("kind")).toInt(), static_cast<int>(MediaKind::Track));
    QCOMPARE(track.value(QStringLiteral("title")).toString(), QStringLiteral("Song"));
    QCOMPARE(track.value(QStringLiteral("artists")).toStringList(), QStringList{QStringLiteral("Artist")});
    QCOMPARE(track.value(QStringLiteral("albumTitle")).toString(), QStringLiteral("Album"));
    QCOMPARE(track.value(QStringLiteral("durationMs")).toLongLong(), 12000);
    QCOMPARE(track.value(QStringLiteral("extra")).toMap().value(QStringLiteral("coverArtId")).toString(),
             QStringLiteral("cover-1"));
    QVERIFY(track.value(QStringLiteral("playable")).toBool());
    QVERIFY(!track.value(QStringLiteral("container")).toBool());
    QVERIFY(!track.contains(QStringLiteral("secret")));

    const QVariantMap album = bridge.searchResults()->get(1);
    QCOMPARE(album.value(QStringLiteral("kind")).toInt(), static_cast<int>(MediaKind::Album));
    QVERIFY(album.value(QStringLiteral("container")).toBool());
    const QVariantMap artist = bridge.searchResults()->get(2);
    QCOMPARE(artist.value(QStringLiteral("kind")).toInt(), static_cast<int>(MediaKind::Artist));
    QVERIFY(artist.value(QStringLiteral("container")).toBool());
}

void MediaBridgeTest::mapsRootAndDirectoryBrowseResponses()
{
    NavidromeFixture fixture;
    fixture.responses.append(
        "{\"subsonic-response\":{\"status\":\"ok\",\"indexes\":{\"artist\":[{\"id\":\"artist-1\",\"name\":\"Artist\"}]}}}");
    fixture.responses.append(
        "{\"subsonic-response\":{\"status\":\"ok\",\"directory\":{\"id\":\"root\",\"child\":[{\"id\":\"folder-1\",\"title\":\"Folder\",\"isDir\":true},{\"id\":\"song-1\",\"title\":\"Song\",\"artist\":\"Artist\"}]}}}");
    fixture.start();

    SourceSessionRegistry registry(nullptr, nullptr);
    QNetworkAccessManager network;
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home")};
    installSession(&registry, id, new NavidromeMappingSession(
                                      navidromeAccount(fixture.server.serverPort()), &network,
                                      &registry));
    MediaBridge bridge(&registry);

    bridge.browse(id.sourceId, id.accountId, {}, static_cast<int>(MediaKind::Directory), 20);
    QTRY_COMPARE(bridge.browseResults()->requestState(), MediaRequestState::Ready);
    QCOMPARE(bridge.browseResults()->get(0).value(QStringLiteral("kind")).toInt(),
             static_cast<int>(MediaKind::Artist));

    bridge.browse(id.sourceId, id.accountId, QStringLiteral("root"),
                  static_cast<int>(MediaKind::Directory), 20);
    QTRY_COMPARE(bridge.browseResults()->requestState(), MediaRequestState::Ready);
    QCOMPARE(bridge.browseResults()->rowCount(), 2);
    QCOMPARE(bridge.browseResults()->get(0).value(QStringLiteral("kind")).toInt(),
             static_cast<int>(MediaKind::Directory));
    QCOMPARE(bridge.browseResults()->get(1).value(QStringLiteral("kind")).toInt(),
             static_cast<int>(MediaKind::Track));
}

void MediaBridgeTest::mapsEmptyAndFailureStatesAndRetriesSavedIntent()
{
    NavidromeFixture fixture;
    fixture.responses.append(
        "{\"subsonic-response\":{\"status\":\"ok\"}}");
    fixture.responses.append("{\"subsonic-response\":{\"status\":\"ok\",\"searchResult3\":{}}}");
    fixture.responses.append(
        "{\"subsonic-response\":{\"status\":\"ok\"}}");
    fixture.responses.append(
        "{\"subsonic-response\":{\"status\":\"ok\",\"indexes\":{\"artist\":[]}}}");
    fixture.statuses = {503, 200, 503, 200};
    fixture.start();

    SourceSessionRegistry registry(nullptr, nullptr);
    QNetworkAccessManager network;
    const MediaId id{QStringLiteral("navidrome"), QStringLiteral("home")};
    installSession(&registry, id, new NavidromeMappingSession(
                                      navidromeAccount(fixture.server.serverPort()), &network,
                                      &registry));
    MediaBridge bridge(&registry);

    bridge.search(QStringLiteral("navidrome/home"), QStringLiteral("Missing"), 7);
    QTRY_COMPARE(bridge.searchResults()->requestState(), MediaRequestState::Failed);
    QCOMPARE(bridge.searchResults()->errorKind(), MediaErrorKind::Network);
    bridge.retry();
    QTRY_COMPARE(bridge.searchResults()->requestState(), MediaRequestState::Empty);

    bridge.browse(id.sourceId, id.accountId, {}, static_cast<int>(MediaKind::Directory), 9);
    QTRY_COMPARE(bridge.browseResults()->requestState(), MediaRequestState::Failed);
    QCOMPARE(bridge.browseResults()->errorKind(), MediaErrorKind::Network);
    bridge.retry();
    QTRY_COMPARE(bridge.browseResults()->requestState(), MediaRequestState::Empty);
    QCOMPARE(fixture.requestLines.size(), 4);
    QVERIFY(fixture.requestLines.at(0).contains("search3.view"));
    QVERIFY(fixture.requestLines.at(1).contains("search3.view"));
    QVERIFY(fixture.requestLines.at(2).contains("getIndexes.view"));
    QVERIFY(fixture.requestLines.at(3).contains("getIndexes.view"));
}

void MediaBridgeTest::ignoresTerminalCallbacksAfterCancellationOrInvalidation()
{
    const MediaId id{QStringLiteral("test-source"), QStringLiteral("home")};
    RegistryHarness harness;
    QVERIFY(harness.initialize());
    QVERIFY(harness.registry.sessionFor(id) != nullptr);
    harness.registry.remove(id.sourceId, id.accountId);
    auto *session = installSession(&harness.registry, id, new LateSession(&harness.registry));
    MediaBridge bridge(&harness.registry);

    bridge.search(QStringLiteral("test-source/home"), QStringLiteral("Song"), 20);
    QTRY_COMPARE(bridge.searchResults()->requestState(), MediaRequestState::Loading);
    bridge.cancel();
    QCOMPARE(session->cancelled.size(), 1);
    session->succeed(QJsonObject{{QStringLiteral("items"), QJsonArray{}}});
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Idle);

    bridge.search(QStringLiteral("test-source/home"), QStringLiteral("Song"), 20);
    QTRY_COMPARE(bridge.searchResults()->requestState(), MediaRequestState::Loading);
    emit harness.registry.sessionInvalidated(id.sourceId, id.accountId);
    session->succeed(QJsonObject{{QStringLiteral("items"), QJsonArray{}}});
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Idle);
}

void MediaBridgeTest::malformedSuccessCallbacksFailActiveModel()
{
    const MediaId id{QStringLiteral("test-source"), QStringLiteral("home")};
    SourceSessionRegistry registry(nullptr, nullptr);
    auto *session = installSession(&registry, id, new LateSession(&registry));
    MediaBridge bridge(&registry);
    const auto requestIsCleared = [&registry, &id](const QUuid &requestId) {
        const auto entry = registry.m_sessions.constFind(registry.keyFor(id));
        QVERIFY(entry != registry.m_sessions.cend());
        QVERIFY(!entry->requests.contains(requestId));
    };

    bridge.search(QStringLiteral("test-source/home"), QStringLiteral("Song"), 20);
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Loading);
    session->succeed(QStringLiteral("browse"), QJsonObject{{QStringLiteral("items"), QJsonArray{}}});
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Failed);
    QCOMPARE(bridge.searchResults()->errorKind(), MediaErrorKind::InvalidRequest);
    requestIsCleared(session->requestId);

    bridge.search(QStringLiteral("test-source/home"), QStringLiteral("Song"), 20);
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Loading);
    session->succeed(QJsonArray{});
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Failed);
    QCOMPARE(bridge.searchResults()->errorKind(), MediaErrorKind::InvalidRequest);
    requestIsCleared(session->requestId);

    const auto malformedItemsFail = [&bridge, session, &requestIsCleared](const QJsonObject &result) {
        bridge.search(QStringLiteral("test-source/home"), QStringLiteral("Song"), 20);
        QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Loading);
        session->succeed(result);
        QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Failed);
        QCOMPARE(bridge.searchResults()->errorKind(), MediaErrorKind::InvalidRequest);
        requestIsCleared(session->requestId);
        session->succeed(QJsonObject{{QStringLiteral("items"), QJsonArray{}}});
        QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Failed);
    };

    malformedItemsFail({});
    malformedItemsFail({{QStringLiteral("items"), QStringLiteral("not-an-array")}});
    malformedItemsFail({{QStringLiteral("items"),
                        QJsonArray{QStringLiteral("not-an-object"),
                                   QJsonObject{{QStringLiteral("title"), QStringLiteral("Missing ID")}}}}});

    bridge.search(QStringLiteral("test-source/home"), QStringLiteral("Song"), 20);
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Loading);
    session->succeed(QJsonObject{{QStringLiteral("items"), QJsonArray{}}});
    QCOMPARE(bridge.searchResults()->requestState(), MediaRequestState::Empty);
    requestIsCleared(session->requestId);
}

QTEST_MAIN(MediaBridgeTest)
#include "tst_MediaBridge.moc"
