#include "KugouApiClient.h"
#include "KugouQrRequests.h"
#include "KugouQrProtocol.h"
#include "KugouQrController.h"
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <cstring>
#include <memory>

namespace {
class Reply final : public QNetworkReply {
public:
    explicit Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request); setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void deliver(QByteArray bytes, int status = 200, bool declareLength = true,
                 NetworkError networkError = NoError, bool notifyReady = true)
    {
        body = std::move(bytes);
        if (status) setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (declareLength) setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        if (networkError != NoError) setError(networkError, "PRIVATE_ERROR_COOKIE");
        setFinished(true);
        if (notifyReady) emit readyRead();
        emit finished();
    }
    void abort() override { aborted = true; setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return body.size() - offset + QNetworkReply::bytesAvailable(); }
    bool aborted = false;
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, qint64(body.size()) - offset);
        if (count <= 0) return -1;
        std::memcpy(data, body.constData() + offset, size_t(count));
        offset += count; return count;
    }
private:
    QByteArray body;
    qint64 offset = 0;
};
class Network final : public QNetworkAccessManager {
public:
    QList<QNetworkRequest> requests;
    QList<QPointer<Reply>> replies;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                QIODevice * = nullptr) override
    {
        Q_ASSERT(operation == GetOperation);
        requests.append(request);
        auto *reply = new Reply(request, this);
        replies.append(reply); return reply;
    }
};
}

class KugouApiClientTest final : public QObject {
    Q_OBJECT
private slots:
    void artistCategoryRequestsUsePrivateWhitelistedRegionsAndAnonymousPolicy()
    {
        using namespace KugouArtistProtocol;
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy ready(&client, &KugouApiClient::succeeded), failed(&client, &KugouApiClient::failed);
        QVector<QUuid> ids;
        for (const auto region : {Region::Chinese, Region::Western, Region::Japanese, Region::Korean}) {
            ids.append(client.getArtist(Operation::Category, {}, 2, 100, region));
            const auto request = network.requests.last(); const QUrlQuery query(request.url());
            QCOMPARE(request.url().scheme(), "https"); QCOMPARE(request.url().host(), "mobilecdnbj.kugou.com");
            QCOMPARE(request.url().path(), "/api/v3/singer/list");
            QCOMPARE(query.queryItems().size(), 6);
            QCOMPARE(query.queryItemValue("type"), QString::number(int(region)));
            QCOMPARE(query.queryItemValue("sex"), "0"); QCOMPARE(query.queryItemValue("page"), "2");
            QCOMPARE(query.queryItemValue("pagesize"), "100"); QCOMPARE(query.queryItemValue("version"), "9108");
            QCOMPARE(query.queryItemValue("plat"), "0");
            QVERIFY(request.rawHeader("Cookie").isEmpty()); QVERIFY(request.rawHeader("Authorization").isEmpty());
            QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
            for (const auto attribute : {QNetworkRequest::CookieLoadControlAttribute,
                 QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::AuthenticationReuseAttribute})
                QCOMPARE(request.attribute(attribute).toInt(), int(QNetworkRequest::Manual));
            QCOMPARE(request.transferTimeout(), 15000); QCOMPARE(request.rawHeader("Accept-Encoding"), "identity");
        }
        QCOMPARE(network.requests.size(), 4); QCOMPARE(QSet<QUuid>(ids.begin(), ids.end()).size(), 4);
        // Deliberately reverse completion order: transport success is correlated
        // by UUID, not an area number or legacy action name.
        for (int i = 3; i >= 0; --i) {
            network.replies[i]->deliver("{\"status\":1,\"data\":{\"singers\":[]}}");
            QCOMPARE(ready.last()[0].toUuid(), ids[i]);
        }
        QCOMPARE(ready.size(), 4); QVERIFY(failed.isEmpty());
        QVERIFY(build(Operation::Category, {}, 1000000, 1, Region::Korean));
    }
    void artistCategoryInvalidParametersFailAsynchronouslyWithoutFallback()
    {
        using namespace KugouArtistProtocol;
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy failed(&client, &KugouApiClient::failed);
        QVector<QUuid> ids;
        ids.append(client.getArtist(Operation::Category)); // no implicit region
        for (const auto number : {-1, 0, 5, 99})
            ids.append(client.getArtist(Operation::Category, {}, 1, 20, static_cast<Region>(number)));
        ids.append(client.getArtist(Operation::Category, "123", 1, 20, Region::Chinese));
        ids.append(client.getArtist(Operation::Popular, {}, 1, 20, Region::Chinese));
        ids.append(client.getArtist(Operation::Tracks, "123", 1, 20, Region::Chinese));
        ids.append(client.getArtist(static_cast<Operation>(99), {}, 1, 20, Region::Chinese));
        for (const auto page : {0, 1000001})
            ids.append(client.getArtist(Operation::Category, {}, page, 20, Region::Chinese));
        for (const auto limit : {0, 101})
            ids.append(client.getArtist(Operation::Category, {}, 1, limit, Region::Chinese));
        const auto cancelled = client.getArtist(Operation::Category); client.cancel(cancelled);
        QVERIFY(failed.isEmpty()); QVERIFY(network.requests.isEmpty());
        QTRY_COMPARE(failed.size(), ids.size());
        for (int i = 0; i < ids.size(); ++i) {
            QCOMPARE(failed[i][0].toUuid(), ids[i]);
            const auto error = qvariant_cast<SourceErrorV2>(failed[i][1]);
            QCOMPARE(error.kind, SourceErrorKindV2::InvalidRequest);
            QCOMPARE(error.messageKey, "kugou.artistInvalidRequest"); QVERIFY(error.detail.isEmpty());
        }
        QVERIFY(network.requests.isEmpty());
    }
    void artistCategoryTransportCancellationAndTlsFailureDoNotRetryOrLeak()
    {
        using namespace KugouArtistProtocol;
        Network network, otherNetwork;
        KugouApiClient client(&network, nullptr), other(&otherNetwork, nullptr);
        QSignalSpy ready(&client, &KugouApiClient::succeeded), failed(&client, &KugouApiClient::failed);
        QSignalSpy otherReady(&other, &KugouApiClient::succeeded);
        const auto cancelled = client.getArtist(Operation::Category, {}, 1, 20, Region::Chinese);
        const auto pending = client.getArtist(Operation::Category, {}, 1, 20, Region::Western);
        const auto separate = other.getArtist(Operation::Category, {}, 1, 20, Region::Japanese);
        client.cancel(cancelled); QVERIFY(network.replies[0]->aborted);
        network.replies[0]->deliver("{\"status\":1,\"token\":\"PRIVATE\"}"); QVERIFY(ready.isEmpty());
        network.replies[1]->deliver("PRIVATE", 0, true, QNetworkReply::SslHandshakeFailedError);
        QCOMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), pending);
        const auto error = qvariant_cast<SourceErrorV2>(failed[0][1]);
        QCOMPARE(error.kind, SourceErrorKindV2::Network); QVERIFY(error.detail.isEmpty()); QVERIFY(!error.httpStatus);
        QCOMPARE(network.requests.size(), 2); QVERIFY(ready.isEmpty());
        otherNetwork.replies[0]->deliver("{\"status\":1,\"data\":{\"info\":[]}}");
        QCOMPARE(otherReady.size(), 1); QCOMPARE(otherReady[0][0].toUuid(), separate);
        auto networkOwner = std::make_unique<Network>(); KugouApiClient owned(networkOwner.get(), nullptr);
        QSignalSpy destroyed(&owned, &KugouApiClient::failed);
        const auto gone = owned.getArtist(Operation::Category, {}, 1, 20, Region::Korean);
        networkOwner.reset(); QTRY_COMPARE(destroyed.size(), 1); QCOMPARE(destroyed[0][0].toUuid(), gone);
    }
    void artistCategoryProjectionReusesStrictCatalogWithoutRawFieldsOrInventedTotal()
    {
        using namespace KugouArtistProtocol;
        const QJsonArray rows{QJsonObject{{"singerid", "123"}, {"singername", "Category Artist"},
            {"imgurl", "file:///PRIVATE"}, {"token", "PRIVATE"}, {"region", "PRIVATE"}}};
        for (const auto *field : {"singers", "info"}) {
            const QJsonObject response{{"status", 1}, {"data", QJsonObject{{field, rows}}}};
            const auto projected = catalog(response, 20); QVERIFY(projected); QVERIFY(!projected->total);
            QCOMPARE(projected->entries.size(), 1); QCOMPARE(projected->entries[0].id, "123");
            QCOMPARE(projected->entries[0].name, "Category Artist");
        }
        QVERIFY(!catalog({{"status", 1}, {"data", QJsonObject{{"singers", rows}, {"info", QJsonArray{}}}}}, 20));
        QVERIFY(!catalog({{"status", 1}, {"data", QJsonObject{{"singers", rows}, {"total", "1"}}}} , 20));
        QVERIFY(!catalog({{"status", 1}, {"data", QJsonObject{{"singers", QJsonArray{rows[0], rows[0]}}}}}, 20));
        const auto empty = catalog({{"status", 1}, {"data", QJsonObject{{"singers", QJsonArray{}}, {"total", 0}}}}, 20);
        QVERIFY(empty); QVERIFY(empty->entries.isEmpty()); QCOMPARE(empty->total, std::optional<int>{0});
    }
    void artistRequestsAreBoundedAnonymousAndFailClosed()
    {
        using namespace KugouArtistProtocol;
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy ready(&client, &KugouApiClient::succeeded), failed(&client, &KugouApiClient::failed);
        const auto popular = client.getArtist(Operation::Popular, {}, 2, 100);
        const auto songs = client.getArtist(Operation::Tracks, "9223372036854775807", 3, 1);
        QVERIFY(popular != songs); QCOMPARE(network.requests.size(), 2);
        for (const auto &request : network.requests) {
            QCOMPARE(request.url().scheme(), "https"); QCOMPARE(request.url().host(), "mobilecdnbj.kugou.com");
            QVERIFY(request.rawHeader("Cookie").isEmpty()); QVERIFY(request.rawHeader("Authorization").isEmpty());
            QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
            for (const auto attribute : {QNetworkRequest::CookieLoadControlAttribute,
                 QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::AuthenticationReuseAttribute})
                QCOMPARE(request.attribute(attribute).toInt(), int(QNetworkRequest::Manual));
            QCOMPARE(request.transferTimeout(), 15000);
            QCOMPARE(request.rawHeader("Accept-Encoding"), "identity");
        }
        QCOMPARE(network.requests[0].url().path(), "/api/v3/singer/rank");
        const QUrlQuery popularQuery(network.requests[0].url()), songsQuery(network.requests[1].url());
        QCOMPARE(popularQuery.queryItemValue("type"), "1"); QCOMPARE(popularQuery.queryItemValue("page"), "2");
        QCOMPARE(popularQuery.queryItemValue("pagesize"), "100"); QCOMPARE(popularQuery.queryItemValue("version"), "9108");
        QCOMPARE(popularQuery.queryItemValue("plat"), "0");
        QCOMPARE(network.requests[1].url().path(), "/api/v3/singer/song");
        QCOMPARE(songsQuery.queryItemValue("singerid"), "9223372036854775807");
        QCOMPARE(songsQuery.queryItemValue("page"), "3"); QCOMPARE(songsQuery.queryItemValue("pagesize"), "1");
        client.cancel(songs); QVERIFY(network.replies[1]->aborted);
        network.replies[1]->deliver("{\"status\":1}"); QCOMPARE(ready.size(), 0);
        network.replies[0]->deliver("PRIVATE", 0, true, QNetworkReply::SslHandshakeFailedError);
        QCOMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), popular);
        const auto error = qvariant_cast<SourceErrorV2>(failed[0][1]);
        QCOMPARE(error.kind, SourceErrorKindV2::Network); QVERIFY(error.detail.isEmpty()); QVERIFY(!error.httpStatus);
        const auto invalid = client.getArtist(Operation::Tracks, "1&token=PRIVATE");
        QCOMPARE(failed.size(), 1); QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(failed[1][0].toUuid(), invalid);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed[1][1]).messageKey, "kugou.artistInvalidRequest");
        QCOMPARE(network.requests.size(), 2);
        for (const auto &id : {QString(), QString("0"), QString("01"), QString("-1"), QString(" 1"),
                              QString("1\n"), QString("9223372036854775808")})
            QVERIFY(!build(Operation::Tracks, id));
        QVERIFY(!build(Operation::Popular, "1")); QVERIFY(!build(static_cast<Operation>(99)));
        for (const auto page : {0, 1000001}) QVERIFY(!build(Operation::Popular, {}, page));
        for (const auto limit : {0, 101}) QVERIFY(!build(Operation::Tracks, "1", 1, limit));
        QVERIFY(build(Operation::Tracks, "1", 1000000, 100));
    }
    void artistClientCorrelatesSuccessAndRejectsDestroyedNetwork()
    {
        auto network = std::make_unique<Network>(); KugouApiClient client(network.get(), nullptr);
        QSignalSpy ready(&client, &KugouApiClient::succeeded), failed(&client, &KugouApiClient::failed);
        const auto popular = client.getArtist(KugouArtistProtocol::Operation::Popular);
        const auto songs = client.getArtist(KugouArtistProtocol::Operation::Tracks, "1");
        network->replies[1]->deliver("{\"status\":1,\"data\":{\"songs\":[]}}");
        QCOMPARE(ready.size(), 1); QCOMPARE(ready[0][0].toUuid(), songs);
        network.reset(); QTRY_COMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), popular);
        QVERIFY(qvariant_cast<SourceErrorV2>(failed[0][1]).detail.isEmpty());
        const auto gone = client.getArtist(KugouArtistProtocol::Operation::Popular);
        QTRY_COMPARE(failed.size(), 2); QCOMPARE(failed[1][0].toUuid(), gone); QCOMPARE(ready.size(), 1);
    }
    void artistCatalogRejectsAmbiguousAliasesAndPreservesAbsentTotals()
    {
        using namespace KugouArtistProtocol;
        const QJsonObject row{{"singerid", "9223372036854775807"}, {"singername", "Artist"},
                              {"imgurl", "https://private/?token=PRIVATE"}, {"token", "PRIVATE"}};
        auto response = [](QJsonObject data) { return QJsonObject{{"status", 1}, {"data", data}}; };
        const QJsonArray rows{row};
        auto result = catalog(response({{"singers", rows}}), 1);
        QVERIFY(result); QVERIFY(!result->total); QCOMPARE(result->entries.size(), 1);
        QCOMPARE(result->entries[0].id, "9223372036854775807"); QCOMPARE(result->entries[0].name, "Artist");
        QVERIFY(catalog(response({{"info", rows}}), 1));
        result = catalog(response({{"singers", rows}, {"info", rows}, {"total", 10}}), 1);
        QVERIFY(result); QCOMPARE(result->total.value(), 10);
        QVERIFY(!catalog(response({{"singers", QJsonArray{}}, {"info", rows}}), 1));
        QVERIFY(!catalog(response({{"singers", rows}, {"info", QJsonArray{}}}), 1));
        QVERIFY(!catalog(response({{"singers", QJsonValue::Null}, {"info", rows}}), 1));
        QVERIFY(!catalog(response({}), 1)); QVERIFY(!catalog(response({{"singers", rows}}), 0));
        QVERIFY(!catalog(response({{"singers", QJsonArray{row, row}}}), 2));
        QVERIFY(!catalog(response({{"singers", QJsonArray{1}}}), 1));
        for (const auto &count : {QJsonValue::fromVariant(QVariant()), QJsonValue("1"), QJsonValue(true),
                                 QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue(2147483648.0)})
            QVERIFY(!catalog(response({{"singers", rows}, {"total", count}}), 1));
        for (const auto &id : {QJsonValue(true), QJsonValue(1.5), QJsonValue(9007199254740992.0),
                              QJsonValue("01"), QJsonValue("9223372036854775808")}) {
            auto bad = row; bad.insert("singerid", id); QVERIFY(!catalog(response({{"singers", QJsonArray{bad}}}), 1));
        }
        QCOMPARE(artistId(9007199254740991.0), "9007199254740991");
        for (const auto &name : {QJsonValue(""), QJsonValue(" \t"), QJsonValue(1),
                                QJsonValue(QString(1025, 'X')), QJsonValue("bad\nname")}) {
            auto bad = row; bad.insert("singername", name); QVERIFY(!catalog(response({{"singers", QJsonArray{bad}}}), 1));
        }
        auto badStatus = response({{"singers", rows}}); badStatus.insert("status", "1");
        QVERIFY(!catalog(badStatus, 1));
        const auto empty = catalog(response({{"singers", QJsonArray{}}, {"total", 0}}), 1);
        QVERIFY(empty); QVERIFY(empty->entries.isEmpty()); QCOMPARE(empty->total.value(), 0);
    }
    void artistSongsWhitelistMetadataWithoutInventingRightsOrPagination()
    {
        using namespace KugouArtistProtocol;
        QJsonObject row{{"hash", QString(32, 'a')}, {"filename", "Artist - Song - Live"}, {"duration", 61.5},
                        {"album_name", "Album"}, {"url", "https://private/?token=PRIVATE"}, {"token", "PRIVATE"},
                        {"320hash", QString(32, 'b')}, {"authors", QJsonArray{QJsonObject{{"token", "PRIVATE"}}}}};
        auto response = [](QJsonObject data) { return QJsonObject{{"status", 1}, {"data", data}}; };
        auto map = [&](QJsonObject candidate) { return tracks(response({{"songs", QJsonArray{candidate}}}), 1); };
        const auto result = map(row); QVERIFY(result); QVERIFY(!result->total);
        const auto projected = result->rows[0].toObject(); QCOMPARE(projected.size(), 5);
        QCOMPARE(projected.value("songname").toString(), "Song - Live");
        QCOMPARE(projected.value("singername").toString(), "Artist");
        QVERIFY(!projected.contains("pay_type")); QVERIFY(!projected.contains("url"));
        QVERIFY(!projected.contains("token")); QVERIFY(!projected.contains("320hash")); QVERIFY(!projected.contains("authors"));
        QVERIFY(tracks(response({{"info", QJsonArray{row}}}), 1));
        QVERIFY(tracks(response({{"songs", QJsonArray{row}}, {"info", QJsonArray{row}}, {"total", 2}}), 1));
        QVERIFY(!tracks(response({{"songs", QJsonArray{}}, {"info", QJsonArray{row}}}), 1));
        for (const auto &pay : {QJsonValue(0), QJsonValue(1), QJsonValue("0"), QJsonValue(true), QJsonValue(QJsonObject{})}) {
            auto candidate = row; candidate.insert("pay_type", pay); const auto mapped = map(candidate); QVERIFY(mapped);
            QCOMPARE(mapped->rows[0].toObject().value("pay_type"), pay.isDouble() ? pay : QJsonValue(QJsonValue::Undefined));
        }
        auto candidate = row; candidate.insert("songname", "Explicit"); candidate.insert("singername", "Named");
        auto mapped = map(candidate); QVERIFY(mapped); QCOMPARE(mapped->rows[0].toObject().value("songname").toString(), "Explicit");
        QCOMPARE(mapped->rows[0].toObject().value("singername").toString(), "Named");
        candidate.insert("songname", ""); QVERIFY(!map(candidate)); // no malformed-title fallback
        for (const auto &filename : {QString("Artist - "), QString(2049, 'X'), QString("bad\nname")}) {
            candidate = row; candidate.insert("filename", filename); QVERIFY(!map(candidate));
        }
        candidate = row; candidate.insert("filename", "Solo"); mapped = map(candidate); QVERIFY(mapped);
        QCOMPARE(mapped->rows[0].toObject().value("songname").toString(), "Solo");
        candidate = row; candidate.insert("hash", "bad"); QVERIFY(!map(candidate));
        candidate = row; candidate.insert("album_name", QJsonObject{}); QVERIFY(!map(candidate));
        candidate = row; candidate.insert("singername", 1); QVERIFY(!map(candidate));
        candidate = row; candidate.insert("duration", QJsonObject{{"token", "PRIVATE"}}); mapped = map(candidate); QVERIFY(mapped);
        QVERIFY(!mapped->rows[0].toObject().contains("duration"));
        QVERIFY(!tracks(response({{"songs", QJsonArray{row}}, {"total", "1"}}), 1));
        QVERIFY(!tracks(response({{"songs", QJsonArray{row, row}}}), 1));
        QVERIFY(!tracks(response({{"songs", QJsonArray{1}}}), 1));
    }
    void rankTlsFailureNeverPublishesOrDowngradesToHttp()
    {
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy ready(&client, &KugouApiClient::succeeded);
        QSignalSpy failed(&client, &KugouApiClient::failed);
        const auto id = client.getRank(KugouRankProtocol::Operation::Catalog);
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests[0].url().scheme(), "https");
        network.replies[0]->deliver("PRIVATE_BODY", 0, true, QNetworkReply::SslHandshakeFailedError);
        QCOMPARE(ready.size(), 0); QCOMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), id);
        const auto error = qvariant_cast<SourceErrorV2>(failed[0][1]);
        QCOMPARE(error.kind, SourceErrorKindV2::Network); QVERIFY(error.detail.isEmpty());
        QVERIFY(!error.httpStatus); QCOMPARE(error.messageKey, "kugou.requestFailed");
        QCoreApplication::processEvents(); QCOMPARE(network.requests.size(), 1);
    }
    void rankRequestsAreAnonymousBoundedAndCancellable()
    {
        using namespace KugouRankProtocol;
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy success(&client, &KugouApiClient::succeeded);
        QSignalSpy failed(&client, &KugouApiClient::failed);
        const auto catalogId = client.getRank(Operation::Catalog);
        const auto tracksId = client.getRank(Operation::Tracks, "8888", 2, 100);
        QVERIFY(catalogId != tracksId); QCOMPARE(network.requests.size(), 2);
        for (const auto &request : network.requests) {
            QCOMPARE(request.url().scheme(), "https");
            QCOMPARE(request.url().host(), "mobilecdnbj.kugou.com");
            QVERIFY(request.rawHeader("Cookie").isEmpty());
            QVERIFY(request.rawHeader("Authorization").isEmpty());
            QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                     int(QNetworkRequest::ManualRedirectPolicy));
            QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
            QCOMPARE(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(), int(QNetworkRequest::Manual));
            QCOMPARE(request.attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt(), int(QNetworkRequest::Manual));
            QCOMPARE(request.transferTimeout(), 15000);
        }
        QCOMPARE(network.requests[0].url().path(), "/api/v3/rank/list");
        const QUrlQuery query(network.requests[1].url());
        QCOMPARE(query.queryItemValue("rankid"), "8888");
        QCOMPARE(query.queryItemValue("page"), "2");
        QCOMPARE(query.queryItemValue("pagesize"), "100");
        QCOMPARE(query.queryItemValue("with_res_tag"), "1");
        client.cancel(tracksId); QVERIFY(network.replies[1]->aborted);
        network.replies[1]->deliver("{\"status\":1}"); QCOMPARE(success.size(), 0);
        network.replies[0]->deliver("{\"status\":1,\"data\":{\"info\":[]}}");
        QCOMPARE(success.size(), 1); QCOMPARE(success[0][0].toUuid(), catalogId);
        const auto invalid = client.getRank(Operation::Tracks, "8888&token=PRIVATE");
        QCOMPARE(failed.size(), 0); QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed[0][0].toUuid(), invalid); QCOMPARE(network.requests.size(), 2);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed[0][1]).messageKey, "kugou.rankInvalidRequest");
        for (const auto &id : {QString(), QString("0"), QString("01"), QString("-1"),
                              QString("2147483648"), QString(" 8888"), QString("8888\n")})
            QVERIFY(!build(Operation::Tracks, id));
        QVERIFY(!build(Operation::Tracks, "8888", 0));
        QVERIFY(!build(Operation::Tracks, "8888", 1000001));
        QVERIFY(!build(Operation::Tracks, "8888", 1, 101));
        QVERIFY(!build(Operation::Tracks, "8888", 1, 0));
        QVERIFY(!build(Operation::Catalog, "8888"));
        QVERIFY(!build(Operation::Catalog, {}, 2));
        QVERIFY(!build(static_cast<Operation>(99)));
        QVERIFY(build(Operation::Tracks, "2147483647", 1000000, 1));
    }
    void rankCatalogValidatesIdentityAndNeverManufacturesFallback()
    {
        using namespace KugouRankProtocol;
        QJsonObject row{{"rankid", 8888}, {"rankname", "Top"}, {"update_frequency", "Daily"},
                        {"intro", "Description"}, {"token", "PRIVATE"}, {"imgurl", "https://private/?token=PRIVATE"}};
        auto response = [&](QJsonArray rows) { return QJsonObject{{"status", 1}, {"data", QJsonObject{{"info", rows}}}}; };
        const auto result = catalog(response({row})); QVERIFY(result); QCOMPARE(result->size(), 1);
        QCOMPARE(result->at(0).id, "8888"); QCOMPARE(result->at(0).title, "Top");
        const auto empty = catalog(response({})); QVERIFY(empty); QVERIFY(empty->isEmpty());
        QVERIFY(!catalog(response({row, row})));
        for (const auto &id : {QJsonValue(), QJsonValue(true), QJsonValue(1.5), QJsonValue("01"),
                              QJsonValue("0"), QJsonValue(2147483648.0)}) {
            auto bad = row; bad.insert("rankid", id); QVERIFY(!catalog(response({bad})));
        }
        for (const auto &title : {QJsonValue(""), QJsonValue(" \t"), QJsonValue(1),
                                 QJsonValue(QString(1025, 'X')), QJsonValue("bad\ntext")}) {
            auto bad = row; bad.insert("rankname", title); QVERIFY(!catalog(response({bad})));
        }
        auto stringId = row; stringId.insert("rankid", "2147483647"); QVERIFY(catalog(response({stringId})));
        QVERIFY(!catalog(response({1}))); QVERIFY(!catalog({}));
        auto badStatus = response({row}); badStatus.insert("status", "1"); QVERIFY(!catalog(badStatus));
        badStatus.insert("status", 0); QVERIFY(!catalog(badStatus));
        QJsonArray many;
        for (int i = 1; i <= 257; ++i) { auto entry = row; entry.insert("rankid", i); many.append(entry); }
        QVERIFY(!catalog(response(many)));
    }
    void rankTrackProjectionPreservesUnknownRightsAndRejectsMalformedPages()
    {
        using namespace KugouRankProtocol;
        QJsonObject row{{"hash", QString(32, 'a')}, {"songname", "Song"}, {"duration", 123},
            {"authors", QJsonArray{QJsonObject{{"author_name", "Artist"}, {"token", "PRIVATE"}}}},
            {"token", "PRIVATE"}, {"url", "https://private/?token=PRIVATE"}, {"320hash", QString(32, 'b')}};
        auto response = [&](QJsonArray rows, QJsonValue total = 1) {
            return QJsonObject{{"status", 1}, {"data", QJsonObject{{"total", total}, {"info", rows}}}};
        };
        auto result = tracks(response({row}), 20); QVERIFY(result); QCOMPARE(result->total, 1);
        const auto projected = result->rows[0].toObject(); QCOMPARE(projected.size(), 4);
        QCOMPARE(projected.value("singername").toString(), "Artist");
        QVERIFY(!projected.contains("pay_type")); QVERIFY(!projected.contains("token"));
        QVERIFY(!projected.contains("authors")); QVERIFY(!projected.contains("url"));
        for (const auto &pay : {QJsonValue(0), QJsonValue(1), QJsonValue("0"), QJsonValue(true),
                               QJsonValue(QJsonObject{{"token", "PRIVATE"}})}) {
            auto candidate = row; candidate.insert("pay_type", pay);
            const auto mapped = tracks(response({candidate}), 1); QVERIFY(mapped);
            QCOMPARE(mapped->rows[0].toObject().value("pay_type"),
                     pay.isDouble() ? pay : QJsonValue(QJsonValue::Undefined));
        }
        for (const auto &total : {QJsonValue(), QJsonValue("1"), QJsonValue(-1), QJsonValue(0),
                                  QJsonValue(1.5), QJsonValue(2147483648.0)})
            QVERIFY(!tracks(response({row}, total), 20));
        QVERIFY(!tracks(response({row, row}, 2), 1));
        QVERIFY(!tracks(response({1}), 20)); QVERIFY(!tracks(response({row}), 0));
        auto bad = row; bad.insert("hash", "bad"); QVERIFY(!tracks(response({bad}), 20));
        bad = row; bad.insert("authors", "Artist"); QVERIFY(!tracks(response({bad}), 20));
        bad = row; bad.insert("authors", QJsonArray{QJsonObject{}}); QVERIFY(!tracks(response({bad}), 20));
        bad = row; bad.insert("songname", ""); QVERIFY(!tracks(response({bad}), 20));
        bad = row; bad.insert("duration", QJsonObject{{"token", "PRIVATE"}});
        const auto clean = tracks(response({bad}), 20); QVERIFY(clean);
        QVERIFY(!clean->rows[0].toObject().contains("duration"));
        auto empty = tracks(response({}, 0), 20); QVERIFY(empty); QVERIFY(empty->rows.isEmpty());
    }
    void qrControllerSerializesPollingAndHandsOffCredentialsOnce()
    {
        using State = KugouQrController::State;
        Network network;
        KugouQrController controller("kugou/one", "one", &network, {1, 2000});
        QSignalSpy confirmed(&controller, &KugouQrController::confirmationReady);
        const auto attempt = controller.start(); QCOMPARE(controller.state(), State::Fetching);
        network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QCOMPARE(controller.state(), State::Waiting); QVERIFY(!controller.qrUrl().isEmpty());
        QTRY_COMPARE(network.requests.size(), 2);
        QTest::qWait(10); QCOMPARE(network.requests.size(), 2); // no overlapping poll
        network.replies[1]->deliver("{\"status\":1,\"data\":{\"status\":1}}");
        QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver("{\"status\":1,\"data\":{\"status\":2}}");
        QCOMPARE(controller.state(), State::Scanned); QTRY_COMPARE(network.requests.size(), 4);
        network.replies[3]->deliver("{\"status\":1,\"data\":{\"status\":4,\"token\":\"PRIVATE\",\"user_id\":123}}");
        QCOMPARE(controller.state(), State::Confirmed); QCOMPARE(confirmed.size(), 1);
        QCOMPARE(confirmed[0][0].toUuid(), attempt); QVERIFY(controller.qrUrl().isEmpty());
        QVERIFY(!controller.takeCredentials(QUuid::createUuid()));
        const auto account = controller.takeAccount(attempt); QVERIFY(account);
        QCOMPARE(account->credentials.userId, "123"); QCOMPARE(account->credentials.token, "PRIVATE");
        QCOMPARE(account->device.sourceInstanceId, "kugou/one");
        QCOMPARE(account->device.requestIdentity.mid.toUtf8(), network.requests[0].rawHeader("mid"));
        const auto record = KugouCredentialRecord::encode(*account); QVERIFY(record);
        QVERIFY(KugouCredentialRecord::decode(*record, "kugou/one", "one", "123"));
        QVERIFY(!controller.takeAccount(attempt));
        QVERIFY(!controller.takeCredentials(attempt)); QTest::qWait(10); QCOMPARE(network.requests.size(), 4);
        controller.start(); QVERIFY(!controller.takeCredentials(attempt));
    }
    void qrControllerCancelRetryAndInstancesFenceOldAttempts()
    {
        Network oneNetwork, twoNetwork;
        KugouQrController one("kugou/one", "one", &oneNetwork, {1, 2000});
        KugouQrController two("kugou/two", "two", &twoNetwork, {1, 2000});
        const auto old = one.start(), other = two.start(); QVERIFY(old != other);
        QVERIFY(oneNetwork.requests[0].rawHeader("mid") != twoNetwork.requests[0].rawHeader("mid"));
        QSignalSpy confirmed(&one, &KugouQrController::confirmationReady);
        one.cancel(); QCOMPARE(one.state(), KugouQrController::State::Cancelled);
        QVERIFY(oneNetwork.replies[0]->aborted);
        const auto current = one.start(); QVERIFY(current != old);
        oneNetwork.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"OLD\"}}");
        QCOMPARE(one.state(), KugouQrController::State::Fetching); QVERIFY(one.qrUrl().isEmpty());
        QCOMPARE(two.state(), KugouQrController::State::Fetching); QCOMPARE(confirmed.size(), 0);
        oneNetwork.replies[1]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"NEW\"}}");
        QTRY_COMPARE(oneNetwork.requests.size(), 3);
        one.cancel();
        oneNetwork.replies[2]->deliver("{\"status\":1,\"data\":{\"status\":4,\"token\":\"OLD_TOKEN\",\"user_id\":1}}");
        QCOMPARE(confirmed.size(), 0); QVERIFY(!one.takeCredentials(current));
    }
    void qrControllerTotalTimeoutRejectsOverdueResponseEvenBeforeTimerDispatch()
    {
        Network network;
        KugouQrController controller("kugou/one", "one", &network, {1, 25});
        QSignalSpy confirmed(&controller, &KugouQrController::confirmationReady);
        const auto attempt = controller.start();
        QTest::qSleep(35); // intentionally do not process the deadline timer
        network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"LATE\"}}");
        QCOMPARE(controller.state(), KugouQrController::State::Expired);
        QVERIFY(controller.qrUrl().isEmpty()); QVERIFY(!controller.takeCredentials(attempt));
        QTest::qWait(10); QCOMPARE(network.requests.size(), 1); QCOMPARE(confirmed.size(), 0);
    }
    void qrControllerDeadlineCancelsInFlightPoll()
    {
        Network network;
        KugouQrController controller("kugou/one", "one", &network, {1, 200});
        controller.start(); network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(network.requests.size(), 2);
        QSignalSpy finished(network.replies[1], &QNetworkReply::finished);
        QTRY_COMPARE(controller.state(), KugouQrController::State::Expired);
        QCOMPARE(finished.size(), 1); QCOMPARE(network.requests.size(), 2);
    }
    void qrControllerMalformedAndNetworkFailureDoNotGrantCredentials()
    {
        Network network;
        KugouQrController controller("kugou/one", "one", &network, {1, 2000});
        auto attempt = controller.start(); network.replies[0]->deliver("{\"status\":\"1\"}");
        QCOMPARE(controller.state(), KugouQrController::State::Failed);
        QCOMPARE(controller.errorKey(), "kugou.qrInvalidResponse"); QVERIFY(!controller.takeCredentials(attempt));
        attempt = controller.start(); network.replies[1]->deliver("PRIVATE_TOKEN_BODY", 403);
        QCOMPARE(controller.errorKey(), "kugou.requestFailed"); QVERIFY(!controller.takeCredentials(attempt));
        KugouQrController invalid("kugou/one", "two", &network, {1, 1000});
        invalid.start(); QCOMPARE(invalid.state(), KugouQrController::State::Failed);
        QCOMPARE(network.requests.size(), 2);
    }
    void qrControllerNotificationsMayCancelRetryOrDestroy()
    {
        Network network;
        KugouQrController cancelled("kugou/one", "one", &network, {1, 1000});
        connect(&cancelled, &KugouQrController::stateChanged, &cancelled, [&cancelled](auto state) {
            if (state == KugouQrController::State::Fetching) cancelled.cancel();
        });
        cancelled.start(); QCOMPARE(network.requests.size(), 0);
        auto *controller = new KugouQrController("kugou/one", "one", &network, KugouQrController::Timing{1, 1000});
        QPointer<KugouQrController> guard(controller);
        connect(controller, &KugouQrController::stateChanged, controller, [controller](auto state) {
            if (state == KugouQrController::State::Waiting) delete controller;
        });
        controller->start(); network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QVERIFY(!guard); QTest::qWait(10); QCOMPARE(network.requests.size(), 1);
        KugouQrController retry("kugou/one", "one", &network, {1, 1000});
        bool restarted = false;
        connect(&retry, &KugouQrController::stateChanged, &retry, [&retry, &restarted](auto state) {
            if (state == KugouQrController::State::Waiting && !restarted) { restarted = true; retry.start(); }
        });
        const auto old = retry.start();
        network.replies[1]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QVERIFY(old != retry.attemptId()); QCOMPARE(retry.state(), KugouQrController::State::Fetching);
        QTest::qWait(10); QCOMPARE(network.requests.size(), 3);
    }
    void qrControllerRetryDuringConfirmationSuppressesOldCredentialSignal()
    {
        Network network;
        KugouQrController controller("kugou/one", "one", &network, {1, 2000});
        QSignalSpy confirmed(&controller, &KugouQrController::confirmationReady);
        const auto old = controller.start();
        network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(network.requests.size(), 2);
        connect(&controller, &KugouQrController::stateChanged, &controller, [&controller](auto state) {
            if (state == KugouQrController::State::Confirmed) controller.start();
        });
        network.replies[1]->deliver("{\"status\":1,\"data\":{\"status\":4,\"token\":\"OLD\",\"user_id\":1}}");
        QCOMPARE(controller.state(), KugouQrController::State::Fetching);
        QVERIFY(old != controller.attemptId()); QCOMPARE(confirmed.size(), 0);
        QVERIFY(!controller.takeCredentials(old)); QCOMPARE(network.requests.size(), 3);
    }
    void qrControllerOwnsOnlyItsProductionNetwork()
    {
        QPointer<QNetworkAccessManager> owned;
        {
            KugouQrController controller("kugou/one", "one"); // no start, no real I/O
            const auto managers = controller.findChildren<QNetworkAccessManager *>();
            QCOMPARE(managers.size(), 1); owned = managers[0];
        }
        QVERIFY(!owned);
        Network borrowed;
        { KugouQrController controller("kugou/one", "one", &borrowed, {1, 1000}); }
        QVERIFY(!borrowed.parent());
    }
    void qrTransportUsesFixedBuilderAndCancellationFencesOldReplies()
    {
        Network network;
        KugouApiClient client(&network, nullptr);
        QSignalSpy success(&client, &KugouApiClient::succeeded), failure(&client, &KugouApiClient::failed);
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto create = client.getQr(KugouQrRequests::Operation::CreateCode, device->requestIdentity, 1700000000);
        const auto oldPoll = client.getQr(KugouQrRequests::Operation::PollCode, device->requestIdentity, 1700000001, "OLD_PRIVATE_KEY");
        QVERIFY(!create.isNull()); QVERIFY(create != oldPoll);
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[0].url().host(), "login-user.kugou.com");
        QCOMPARE(network.requests[0].url().path(), "/v2/qrcode");
        QCOMPARE(network.requests[1].url().path(), "/v2/get_userinfo_qrcode");
        QCOMPARE(network.requests[1].rawHeader("mid"), device->requestIdentity.mid.toUtf8());
        QVERIFY(network.requests[0].hasRawHeader("User-Agent"));
        QVERIFY(!network.requests[0].hasRawHeader("Cookie"));
        client.cancel(oldPoll); QVERIFY(network.replies[1]->aborted);
        const auto current = client.getQr(KugouQrRequests::Operation::PollCode, device->requestIdentity, 1700000002, "NEW_PRIVATE_KEY");
        network.replies[1]->deliver("{\"status\":1,\"data\":{\"status\":4,\"token\":\"OLD_PRIVATE_TOKEN\",\"user_id\":1}}");
        QCOMPARE(success.size(), 0); QCOMPARE(failure.size(), 0);
        network.replies[2]->deliver("{\"status\":1,\"data\":{\"status\":1}}");
        QCOMPARE(success.size(), 1); QCOMPARE(success[0][0].toUuid(), current);
        network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"CODE\"}}");
        QCOMPARE(success.size(), 2); QCOMPARE(success[1][0].toUuid(), create);
        const auto pending = client.getQr(KugouQrRequests::Operation::CreateCode, device->requestIdentity, 1);
        QVERIFY(!pending.isNull()); client.cancelAll();
        network.replies[3]->deliver("{\"status\":1}"); QCOMPARE(success.size(), 2);
    }
    void qrInvalidInputAndNetworkLossAreDeferredAndSanitized()
    {
        auto network = std::make_unique<Network>();
        KugouApiClient client(network.get(), nullptr);
        QSignalSpy failure(&client, &KugouApiClient::failed), success(&client, &KugouApiClient::succeeded);
        const auto invalid = client.getQr(KugouQrRequests::Operation::PollCode, {}, 1, "PRIVATE_KEY");
        QCOMPARE(failure.size(), 0); QCOMPARE(network->requests.size(), 0);
        QTRY_COMPARE(failure.size(), 1); QCOMPARE(failure[0][0].toUuid(), invalid);
        auto error = qvariant_cast<SourceErrorV2>(failure[0][1]);
        QCOMPARE(error.kind, SourceErrorKindV2::InvalidRequest); QVERIFY(error.detail.isEmpty());
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto pending = client.getQr(KugouQrRequests::Operation::CreateCode, device->requestIdentity, 1);
        network.reset(); QTRY_COMPARE(failure.size(), 2);
        QCOMPARE(failure[1][0].toUuid(), pending);
        error = qvariant_cast<SourceErrorV2>(failure[1][1]);
        QCOMPARE(error.kind, SourceErrorKindV2::Unavailable); QVERIFY(error.detail.isEmpty());
        const auto absent = client.getQr(KugouQrRequests::Operation::CreateCode, device->requestIdentity, 1);
        client.cancel(absent); QCoreApplication::processEvents(); QCOMPARE(failure.size(), 2);
        QCOMPARE(success.size(), 0);
    }
    void qrDeviceIdentityIsEphemeralAndInstanceBound()
    {
        using namespace KugouQrProtocol;
        QVERIFY(!createDevice("kugou/one", "two"));
        QVERIFY(!createDevice("kugou/", ""));
        QVERIFY(!createDevice("kugou/one\n", "one\n"));
        const auto one = createDevice("kugou/one", "one"), two = createDevice("kugou/two", "two");
        QVERIFY(one && two);
        QCOMPARE(one->sourceInstanceId, "kugou/one"); QCOMPARE(one->accountId, "one");
        QVERIFY(one->guid != two->guid); QVERIFY(one->requestIdentity.dfid != two->requestIdentity.dfid);
        QVERIFY(one->requestIdentity.mid != two->requestIdentity.mid);
        QVERIFY(QUuid(one->guid).version() == QUuid::Random);
        QCOMPARE(one->requestIdentity.mid, decimalDigest(QCryptographicHash::hash(one->guid.toUtf8(), QCryptographicHash::Md5)));
        QVERIFY(KugouQrRequests::build(KugouQrRequests::Operation::CreateCode, one->requestIdentity, 1));
        QCOMPARE(decimalDigest(QByteArray(16, '\0')), "0");
        QCOMPARE(decimalDigest(QByteArray(16, char(0xff))), "340282366920938463463374607431768211455");
        QCOMPARE(decimalDigest(QCryptographicHash::hash("00112233-4455-4677-8899-aabbccddeeff", QCryptographicHash::Md5)),
                 "303182012337535952547182526388775132166");
        QByteArray lowDigest(16, '\0'); lowDigest[13] = 1;
        QCOMPARE(decimalDigest(lowDigest), "65536");
        QVERIFY(decimalDigest(QByteArray(15, '\0')).isEmpty());
    }
    void qrResponseMappingRequiresExplicitStateAndAccountIdentity()
    {
        using namespace KugouQrProtocol;
        auto wrap = [](QJsonObject data) { return QJsonObject{{"status", 1}, {"data", data}}; };
        auto code = mapCode(wrap({{"qrcode", "opaque&+#/%26"}, {"token", "IGNORED"}}));
        QVERIFY(code); QCOMPARE(code->landingUrl.host(), "h5.kugou.com");
        QCOMPARE(QUrlQuery(code->landingUrl).queryItems().size(), 2);
        QCOMPARE(QUrlQuery(code->landingUrl).queryItemValue("qrcode", QUrl::FullyDecoded), code->key);
        QVERIFY(!code->landingUrl.hasFragment());
        for (const auto &bad : {QString{}, QString("key\n"), QString(1025, 'a')})
            QVERIFY(!mapCode(wrap({{"qrcode", bad}})));
        QVERIFY(!mapCode(wrap({{"qrcode", 123}})));
        QVERIFY(!mapCode({{"status", "1"}, {"data", QJsonObject{{"qrcode", "key"}}}}));
        QVERIFY(!mapCode({{"status", 1}, {"data", QJsonArray{}}}));
        for (int status : {0, 1, 2}) {
            const auto poll = mapPoll(wrap({{"status", status}, {"token", "SHOULD_NOT_ESCAPE"}, {"user_id", 7}}));
            QVERIFY(poll); QVERIFY(!poll->credentials);
            QCOMPARE(poll->state, status == 0 ? PollState::Expired : status == 1 ? PollState::Waiting : PollState::Scanned);
        }
        QJsonObject data{{"status", 4}, {"token", "PRIVATE_TOKEN="}, {"user_id", 123}, {"nickname", "ignored"}};
        auto poll = mapPoll(wrap(data)); QVERIFY(poll && poll->credentials);
        QCOMPARE(poll->state, PollState::Confirmed); QCOMPARE(poll->credentials->userId, "123");
        QCOMPARE(poll->credentials->token, "PRIVATE_TOKEN=");
        data.insert("userid", "123"); QVERIFY(mapPoll(wrap(data)));
        data.insert("userid", "124"); QVERIFY(!mapPoll(wrap(data)));
        data.remove("user_id"); QVERIFY(mapPoll(wrap(data)));
        data.remove("userid"); QVERIFY(!mapPoll(wrap(data)));
        for (const auto &id : {QJsonValue("01"), QJsonValue("0"), QJsonValue(" 1"), QJsonValue(true),
                              QJsonValue(-1), QJsonValue(1.5), QJsonValue(9007199254740992.0), QJsonValue()}) {
            data.insert("user_id", id); QVERIFY(!mapPoll(wrap(data)));
        }
        data.insert("user_id", "9007199254740993"); QVERIFY(mapPoll(wrap(data)));
        for (const auto &token : {QJsonValue(""), QJsonValue("x; userid=1"), QJsonValue("x\r\n"),
                                 QJsonValue(123), QJsonValue(QString(4097, 'x'))}) {
            data.insert("token", token); QVERIFY(!mapPoll(wrap(data)));
        }
        data.insert("token", "valid");
        for (const auto &status : {QJsonValue("4"), QJsonValue(3), QJsonValue(4.5), QJsonValue(true), QJsonValue()}) {
            data.insert("status", status); QVERIFY(!mapPoll(wrap(data)));
        }
        QVERIFY(!mapPoll({{"status", 0}, {"data", QJsonObject{{"status", 4}, {"token", "valid"}, {"user_id", 1}}}}));
    }
    void qrRequestsAreDeterministicPrivateAndStrict()
    {
        using namespace KugouQrRequests;
        QCOMPARE(signature({{"a", "1"}, {"b", QString::fromUtf8("中文")}}),
                 QByteArray("bf777163e4a7ecfd2b4d8de8de4b6baa"));
        const DeviceIdentity identity{"ABCDEFGHIJKLMNOPQRSTUVWX", "123456789012345678901234567890"};
        const auto create = build(Operation::CreateCode, identity, 1700000000);
        QVERIFY(create);
        QCOMPARE(create->url().host(), "login-user.kugou.com");
        QCOMPARE(create->url().scheme(), "https");
        QCOMPARE(create->url().path(), "/v2/qrcode");
        const QUrlQuery query(create->url());
        QCOMPARE(query.queryItems().size(), 11);
        QCOMPARE(query.queryItemValue("qrcode_txt", QUrl::FullyDecoded),
                 "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=1001&");
        QMap<QString, QString> unsignedParameters;
        for (const auto &pair : query.queryItems(QUrl::FullyDecoded))
            if (pair.first != "signature") unsignedParameters.insert(pair.first, pair.second);
        QCOMPARE(query.queryItemValue("signature").toLatin1(), signature(unsignedParameters));
        QCOMPARE(create->rawHeader("mid"), identity.mid.toUtf8());
        QCOMPARE(create->rawHeader("dfid"), identity.dfid.toUtf8());
        QCOMPARE(create->rawHeader("clienttime"), QByteArray("1700000000"));
        QVERIFY(!create->hasRawHeader("Cookie")); QVERIFY(!create->hasRawHeader("Authorization"));
        QCOMPARE(create->transferTimeout(), 15000);
        QCOMPARE(create->attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(create->attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(create->attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(create->attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        const auto poll = build(Operation::PollCode, identity, 1700000000, "a&b=+#/%26opaque");
        QVERIFY(poll); QCOMPARE(poll->url().path(), "/v2/get_userinfo_qrcode");
        QCOMPARE(QUrlQuery(poll->url()).queryItemValue("qrcode", QUrl::FullyDecoded), "a&b=+#/%26opaque");
        QCOMPARE(QUrlQuery(poll->url()).queryItems().size(), 10);
        QVERIFY(!poll->url().hasFragment());
        QVERIFY(create->url() == build(Operation::CreateCode, identity, 1700000000)->url());
        QVERIFY(create->url() != build(Operation::CreateCode, identity, 1700000001)->url());
        auto other = identity; other.dfid = "ABCDEFGHIJKLMNOPQRSTUVWY";
        QVERIFY(create->url() != build(Operation::CreateCode, other, 1700000000)->url());
        QVERIFY(!build(Operation::CreateCode, identity, 0));
        QVERIFY(!build(Operation::CreateCode, identity, 253402300800LL));
        QVERIFY(!build(Operation::CreateCode, identity, 1, "unexpected"));
        QVERIFY(!build(Operation::PollCode, identity, 1));
        QVERIFY(!build(Operation::PollCode, identity, 1, QString(1025, 'a')));
        QVERIFY(!build(Operation::PollCode, identity, 1, "key\r\nInjected"));
        QVERIFY(!build(Operation::PollCode, identity, 1, " key"));
        QVERIFY(!build(Operation(99), identity, 1));
        other = identity; other.dfid.append('\n'); QVERIFY(!build(Operation::CreateCode, other, 1));
        other = identity; other.dfid[0] = 'a'; QVERIFY(!build(Operation::CreateCode, other, 1));
        other = identity; other.mid = "01"; QVERIFY(!build(Operation::CreateCode, other, 1));
        other.mid = "-1"; QVERIFY(!build(Operation::CreateCode, other, 1));
        other.mid = QString(40, '9'); QVERIFY(!build(Operation::CreateCode, other, 1));
        other.mid = "340282366920938463463374607431768211456";
        QVERIFY(!build(Operation::CreateCode, other, 1));
        other.mid = "340282366920938463463374607431768211455";
        QVERIFY(build(Operation::CreateCode, other, 1));
    }
    void endpointsAreFixedAndRequestsDoNotBorrowAuthentication()
    {
        Network network;
        KugouApiClient client(&network, nullptr);
        QSignalSpy success(&client, &KugouApiClient::succeeded);
        const auto id = client.get(KugouApiClient::Endpoint::SearchTracks,
                                  QUrlQuery({{"keyword", "A&B 中文"}, {"page", "1"}}));
        QVERIFY(!id.isNull()); QCOMPARE(network.requests.size(), 1);
        const auto request = network.requests.first();
        QCOMPARE(request.url().scheme(), "https");
        QCOMPARE(request.url().host(), "mobilecdn.kugou.com");
        QCOMPARE(request.url().path(), "/api/v3/search/song");
        QCOMPARE(QUrlQuery(request.url()).queryItemValue("keyword", QUrl::FullyDecoded), "A&B 中文");
        QVERIFY(!request.hasRawHeader("Cookie"));
        QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(request.transferTimeout(), 15000);
        network.replies.first()->deliver("<!--KG_TAG_RES_START-->{\"status\":1}<!--KG_TAG_RES_END-->");
        QCOMPARE(success.size(), 1); QCOMPARE(success.first().first().toUuid(), id);
        QCOMPARE(qvariant_cast<QJsonObject>(success.first()[1]).value("status").toInt(), 1);
        client.get(KugouApiClient::Endpoint::ResolveTrack, QUrlQuery({{"cmd", "playInfo"}, {"hash", "ABC"}}));
        QCOMPARE(network.requests.last().url().host(), "m.kugou.com");
        QCOMPARE(network.requests.last().url().path(), "/app/i/getSongInfo.php");
    }
    void concurrentRequestsKeepIdentityAndCancelSuppressesLateCallbacks()
    {
        Network network;
        KugouApiClient client(&network, nullptr);
        QSignalSpy success(&client, &KugouApiClient::succeeded), failure(&client, &KugouApiClient::failed);
        const auto first = client.get(KugouApiClient::Endpoint::SearchTracks);
        const auto second = client.get(KugouApiClient::Endpoint::SearchTracks);
        const auto third = client.get(KugouApiClient::Endpoint::ResolveTrack);
        QVERIFY(first != second && first != third && second != third);
        network.replies[1]->deliver("{\"sequence\":2}");
        client.cancel(first); QVERIFY(network.replies[0]->aborted);
        network.replies[0]->deliver("{\"sequence\":1}");
        network.replies[2]->deliver("{\"sequence\":3}");
        QCOMPARE(success.size(), 2); QCOMPARE(failure.size(), 0);
        QCOMPARE(success[0][0].toUuid(), second); QCOMPARE(success[1][0].toUuid(), third);
        emit network.replies[2]->finished(); QCOMPARE(success.size(), 2);
    }
    void invalidEndpointAndMissingNetworkAreDeferredAndCancelable()
    {
        Network network;
        KugouApiClient client(&network, nullptr);
        QSignalSpy failure(&client, &KugouApiClient::failed);
        const auto id = client.get(static_cast<KugouApiClient::Endpoint>(99));
        QCOMPARE(failure.size(), 0); QCOMPARE(network.requests.size(), 0);
        QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(failure[0][0].toUuid(), id);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failure[0][1]).kind, SourceErrorKindV2::InvalidRequest);
        client.cancel(client.get(static_cast<KugouApiClient::Endpoint>(99)));
        QCoreApplication::processEvents(); QCOMPARE(failure.size(), 1);
        KugouApiClient unavailable(nullptr, nullptr);
        QSignalSpy missing(&unavailable, &KugouApiClient::failed);
        unavailable.get(KugouApiClient::Endpoint::SearchTracks);
        QTRY_COMPARE(missing.size(), 1);
        QCOMPARE(qvariant_cast<SourceErrorV2>(missing[0][1]).kind, SourceErrorKindV2::Unavailable);
    }
    void httpAndNetworkFailuresAreTypedAndSanitized_data()
    {
        QTest::addColumn<int>("status"); QTest::addColumn<int>("kind"); QTest::addColumn<bool>("retryable");
        QTest::newRow("unauthorized") << 401 << int(SourceErrorKindV2::Authentication) << false;
        QTest::newRow("forbidden") << 403 << int(SourceErrorKindV2::Authorization) << false;
        QTest::newRow("not-found") << 404 << int(SourceErrorKindV2::NotFound) << false;
        QTest::newRow("rate-limited") << 429 << int(SourceErrorKindV2::RateLimited) << true;
        QTest::newRow("server") << 503 << int(SourceErrorKindV2::Unavailable) << true;
        QTest::newRow("redirect-not-followed") << 302 << int(SourceErrorKindV2::Unavailable) << false;
        QTest::newRow("network") << 0 << int(SourceErrorKindV2::Network) << true;
    }
    void httpAndNetworkFailuresAreTypedAndSanitized()
    {
        QFETCH(int, status); QFETCH(int, kind); QFETCH(bool, retryable);
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy success(&client, &KugouApiClient::succeeded), failure(&client, &KugouApiClient::failed);
        const auto id = client.get(KugouApiClient::Endpoint::SearchTracks);
        network.replies[0]->deliver("PRIVATE_BODY_TOKEN", status, true, QNetworkReply::UnknownNetworkError);
        QCOMPARE(success.size(), 0); QCOMPARE(failure.size(), 1); QCOMPARE(failure[0][0].toUuid(), id);
        const auto result = qvariant_cast<SourceErrorV2>(failure[0][1]);
        QCOMPARE(int(result.kind), kind); QCOMPARE(result.retryable, retryable); QVERIFY(result.detail.isEmpty());
        QCOMPARE(result.messageKey, "kugou.requestFailed"); QCOMPARE(result.httpStatus.has_value(), status != 0);
        if (status) QCOMPARE(*result.httpStatus, status);
    }
    void objectParsingAndSizeLimits_data()
    {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<bool>("declared"); QTest::addColumn<QString>("key");
        QTest::newRow("invalid") << QByteArray("PRIVATE_INVALID_TOKEN") << true << QString("kugou.invalidResponse");
        QTest::newRow("array") << QByteArray("[]") << true << QString("kugou.invalidResponse");
        QTest::newRow("internal-wrapper-not-rewritten") << QByteArray("{\"text\":\"<!--KG_TAG_RES_START-->\"}") << true << QString();
        QTest::newRow("empty-object-valid") << QByteArray("{}") << true << QString();
        QTest::newRow("oversized-declared") << QByteArray(KugouApiClient::MaximumBodyBytes + 1, 'x') << true << QString("kugou.responseTooLarge");
        QTest::newRow("oversized-streamed") << QByteArray(KugouApiClient::MaximumBodyBytes + 1, 'x') << false << QString("kugou.responseTooLarge");
    }
    void objectParsingAndSizeLimits()
    {
        QFETCH(QByteArray, body); QFETCH(bool, declared); QFETCH(QString, key);
        Network network; KugouApiClient client(&network, nullptr);
        QSignalSpy success(&client, &KugouApiClient::succeeded), failure(&client, &KugouApiClient::failed);
        client.get(KugouApiClient::Endpoint::SearchTracks); network.replies[0]->deliver(body, 200, declared);
        QCOMPARE(success.size(), key.isEmpty() ? 1 : 0); QCOMPARE(failure.size(), key.isEmpty() ? 0 : 1);
        if (!key.isEmpty()) {
            const auto result = qvariant_cast<SourceErrorV2>(failure[0][1]);
            QCOMPARE(result.messageKey, key); QVERIFY(result.detail.isEmpty()); QVERIFY(!result.retryable);
        } else if (body.contains("text")) {
            QCOMPARE(qvariant_cast<QJsonObject>(success[0][1]).value("text").toString(), "<!--KG_TAG_RES_START-->");
        }
    }
    void destructionAndInstanceIsolation()
    {
        auto network = std::make_unique<Network>();
        KugouApiClient survivor(network.get(), nullptr);
        QSignalSpy failure(&survivor, &KugouApiClient::failed);
        survivor.get(KugouApiClient::Endpoint::SearchTracks);
        network.reset(); QTRY_COMPARE(failure.size(), 1);
        QCoreApplication::processEvents(); QCOMPARE(failure.size(), 1);

        Network shared;
        auto first = std::make_unique<KugouApiClient>(&shared, nullptr);
        KugouApiClient second(&shared, nullptr);
        QSignalSpy success(&second, &KugouApiClient::succeeded);
        first->get(KugouApiClient::Endpoint::SearchTracks);
        const auto id = second.get(KugouApiClient::Endpoint::SearchTracks);
        first.reset(); QVERIFY(shared.replies[0]->aborted); QVERIFY(!shared.replies[1]->aborted);
        shared.replies[1]->deliver("{}"); QCOMPARE(success.size(), 1); QCOMPARE(success[0][0].toUuid(), id);
    }
    void failureObserverMayDeleteClientDuringFinalConsumption()
    {
        Network network;
        QPointer<KugouApiClient> client = new KugouApiClient(&network, nullptr);
        connect(client, &KugouApiClient::failed, &network, [&client] { delete client.data(); });
        client->get(KugouApiClient::Endpoint::SearchTracks);
        // finish() consumes buffered bytes too; guard the emitting-failure path.
        network.replies[0]->deliver(QByteArray(KugouApiClient::MaximumBodyBytes + 1, 'x'),
                                    200, false, QNetworkReply::NoError, false);
        QVERIFY(!client);
    }
};
QTEST_GUILESS_MAIN(KugouApiClientTest)
#include "tst_KugouApiClient.moc"
