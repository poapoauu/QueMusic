#include "KugouApiClient.h"
#include "KugouQrRequests.h"
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
