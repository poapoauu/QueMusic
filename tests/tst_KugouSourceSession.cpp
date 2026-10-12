#include "KugouSourceSession.h"
#include "KugouMappers.h"
#include "KugouCredentialRecord.h"
#include "v2/SourceSecretsV2.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <cstring>
#include <memory>

namespace {
const QString hash = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void deliver(const QJsonObject &object)
    {
        bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true);
        emit readyRead(); emit finished();
    }
    void abort() override { aborted = true; setFinished(true); emit finished(); }
    bool aborted = false;
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, qint64(bytes.size()) - offset);
        if (count <= 0) return -1;
        std::memcpy(data, bytes.constData() + offset, size_t(count)); offset += count; return count;
    }
private:
    QByteArray bytes; qint64 offset = 0;
};
class Network final : public QNetworkAccessManager {
public:
    QList<QPointer<Reply>> replies;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice * = nullptr) override
    {
        requests.append(request); auto *reply = new Reply(request, this); replies.append(reply); return reply;
    }
};
SourceConfigurationV2 config(const QString &account = "one")
{
    return {"org.quemusic.source.kugou", "kugou", "kugou/" + account, account, "Kugou " + account, {}, {}};
}
PageQueryV2 search(const QString &account = "one")
{
    PageQueryV2 query; query.page = MusicPageKindV2::Search; query.section = PageSectionKindV2::Tracks;
    query.scope.sourceInstanceId = "kugou/" + account; query.searchText = "测试 A&B"; query.limit = 1;
    return query;
}
QJsonObject track(int pay = 0, const QString &id = hash)
{
    return {{"hash", id}, {"songname", "Song"}, {"singername", "Artist"},
            {"duration", 61.5}, {"pay_type", pay}, {"url", "file:///PRIVATE"}, {"token", "PRIVATE"}};
}
QJsonObject page(const QJsonArray &rows = {track()}, int total = 2)
{
    return {{"status", 1}, {"data", QJsonObject{{"info", rows}, {"total", total}}}};
}
PageQueryV2 ranks(const QString &account = "one")
{
    auto query = search(account); query.page = MusicPageKindV2::Category;
    query.section = PageSectionKindV2::Playlists; query.searchText.clear(); return query;
}
PageQueryV2 rankTracks(const QString &id = "rank/8888", const QString &account = "one")
{
    auto query = ranks(account); query.section = PageSectionKindV2::Tracks;
    query.filters = {{"playlistId", id}}; return query;
}
QJsonObject catalog()
{
    return {{"status", 1}, {"data", QJsonObject{{"info", QJsonArray{
        QJsonObject{{"rankid", 8888}, {"rankname", "Top"}, {"update_frequency", "Daily"}},
        QJsonObject{{"rankid", 6666}, {"rankname", "Rising"}}}}}}};
}
QJsonObject rankTrack(int pay = 0)
{
    auto row = track(pay); row.remove("singername");
    row.insert("authors", QJsonArray{QJsonObject{{"author_name", "Rank Artist"}}}); return row;
}
QJsonObject verifiedResource()
{
    return {{"status", 1}, {"data", QJsonObject{{"hash", hash}, {"songName", "Verified Song"},
        {"author_name", "Verified Artist"}, {"timeLength", 62}, {"pay_type", 0},
        {"url", "https://cdn.kugou.com/song?key=PRIVATE"}, {"token", "PRIVATE"}}}};
}
MediaRefV2 media(const QString &account = "one", const QString &id = hash)
{
    return {"kugou", "kugou/" + account, account, MediaEntityTypeV2::Track, id};
}
}

class KugouSourceSessionTest final : public QObject {
    Q_OBJECT
private slots:
    void rankCatalogPublishesStandardIdentityAndPagesOneBoundedSnapshot()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        auto query = ranks(); const auto id = session.fetchPage(query);
        QCOMPARE(ready.size(), 0); QTRY_COMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests[0].url().path(), "/api/v3/rank/list");
        network.replies[0]->deliver(catalog()); QCOMPARE(ready.size(), 1); QCOMPARE(ready[0][0].toUuid(), id);
        auto section = qvariant_cast<PageResultV2>(ready[0][1]).sections[0];
        QCOMPARE(section.kind, PageSectionKindV2::Playlists); QCOMPARE(section.items.size(), 1);
        const MediaRefV2 ref{"kugou", "kugou/one", "one", MediaEntityTypeV2::Playlist, "rank/8888"};
        QCOMPARE(section.items[0].ref, ref); QCOMPARE(section.items[0].title, "Top");
        QCOMPARE(section.items[0].availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unsupported);
        QVERIFY(section.items[0].metadata.isEmpty()); QVERIFY(section.hasMore); QVERIFY(!session.lookupItem(ref));
        query.cursor = section.nextCursor;
        const auto cancelled = session.fetchPage(query); session.cancel(cancelled);
        QCoreApplication::processEvents(); QCOMPARE(ready.size(), 1); QCOMPARE(network.requests.size(), 1);
        session.fetchPage(query); QCOMPARE(ready.size(), 1);
        QTRY_COMPARE(ready.size(), 2); QCOMPARE(network.requests.size(), 1);
        section = qvariant_cast<PageResultV2>(ready[1][1]).sections[0];
        QCOMPARE(section.items[0].ref.entityId, "rank/6666"); QVERIFY(!section.hasMore);
        session.fetchPage(ranks()); QTRY_COMPARE(network.requests.size(), 2);
        network.replies[1]->deliver(catalog()); session.fetchPage(query); QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed[0][1]).messageKey, "kugou.cursor.invalid");
        QCOMPARE(network.requests.size(), 2);
        session.fetchPage(ranks()); QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver({{"status", 1}, {"data", QJsonObject{{"info", QJsonArray{}}}}});
        const auto empty = qvariant_cast<PageResultV2>(ready.last()[1]);
        QVERIFY(empty.sections[0].items.isEmpty()); QVERIFY(!empty.sections[0].hasMore);
        QCOMPARE(empty.sourceStates.value("kugou/one").state, SourcePageLoadStateV2::Empty);
    }
    void rankTrackCursorBindsRankModeLimitAndInstance()
    {
        Network network, otherNetwork;
        KugouSourceSession session(config(), &network, nullptr), other(config("two"), &otherNetwork, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
        QSignalSpy otherFailed(&other, &IMusicSourceSessionV2::requestFailed);
        session.open(); other.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(other.state(), SourceSessionStateV2::Ready);
        auto query = rankTracks(); session.fetchPage(query); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({rankTrack()}, 2));
        const auto section = qvariant_cast<PageResultV2>(ready[0][1]).sections[0];
        QCOMPARE(section.items[0].ref, media()); QCOMPARE(section.items[0].subtitle, "Rank Artist");
        QVERIFY(section.hasMore); query.cursor = section.nextCursor;
        for (int variant = 0; variant < 4; ++variant) {
            auto wrong = query;
            if (variant == 0) wrong.filters = {{"playlistId", "rank/6666"}};
            if (variant == 1) wrong.limit = 2;
            if (variant == 2) { wrong = search(); wrong.cursor = query.cursor; }
            if (variant == 3) { wrong = ranks(); wrong.cursor = query.cursor; }
            session.fetchPage(wrong); QTRY_COMPARE(failed.size(), variant + 1);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failed.last()[1]).messageKey, "kugou.cursor.invalid");
        }
        auto foreign = query; foreign.scope.sourceInstanceId = "kugou/two";
        other.fetchPage(foreign); QTRY_COMPARE(otherFailed.size(), 1); QVERIFY(otherNetwork.requests.isEmpty());
        session.fetchPage(query); QTRY_COMPARE(network.requests.size(), 2);
        const QUrlQuery wire(network.requests[1].url()); QCOMPARE(wire.queryItemValue("rankid"), "8888");
        QCOMPARE(wire.queryItemValue("page"), "2"); QCOMPARE(wire.queryItemValue("pagesize"), "1");
        network.replies[1]->deliver(page({}, 2)); QVERIFY(!qvariant_cast<PageResultV2>(ready.last()[1]).sections[0].hasMore);
        session.close(); session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(query); QTRY_COMPARE(failed.size(), 5); QCOMPARE(network.requests.size(), 2);
    }
    void rankRightsIntersectSearchAndLateFreeResponses()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(rankTracks()); session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 2);
        network.replies[1]->deliver(page({track(1)}, 1));
        network.replies[0]->deliver(page({rankTrack(0)}, 1));
        QCOMPARE(ready.size(), 2);
        QCOMPARE(qvariant_cast<PageResultV2>(ready.last()[1]).sections[0].items[0]
            .availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        QVERIFY(session.lookupItem(media()));
        session.resolveStream(media()); QTRY_COMPARE(failed.size(), 1); QCOMPARE(network.requests.size(), 2);
        session.close(); session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        auto query = rankTracks(); query.limit = 2; session.fetchPage(query); QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver(page({rankTrack(0), rankTrack(1)}, 2));
        const auto items = qvariant_cast<PageResultV2>(ready.last()[1]).sections[0].items;
        QCOMPARE(items.size(), 1); QCOMPARE(items[0].availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        QCOMPARE(session.lookupItem(media())->availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
    }
    void rankPermissionDowngradeWhileResourceIsPendingBlocksTheStream()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy streams(&session, &IMusicSourceSessionV2::streamReady);
        QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(rankTracks()); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({rankTrack()}, 1));
        const auto id = session.resolveStream(media()); QTRY_COMPARE(network.requests.size(), 2);
        session.fetchPage(rankTracks()); QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver(page({rankTrack(1)}, 1));
        network.replies[1]->deliver(verifiedResource()); QCOMPARE(streams.size(), 0);
        QCOMPARE(failed.size(), 1); QCOMPARE(failed[0][0].toUuid(), id);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failed[0][1]).kind, SourceErrorKindV2::Authorization);
    }
    void rankQueriesRejectForgedFiltersAndMalformedResponsesWithoutFallback()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        for (int variant = 0; variant < 8; ++variant) {
            auto query = rankTracks();
            if (variant == 0) query.filters.clear();
            if (variant == 1) query.filters = {{"playlistId", "8888"}};
            if (variant == 2) query.filters = {{"playlistId", "rank/01"}};
            if (variant == 3) query.filters = {{"playlistId", "rank/8888&token=PRIVATE"}};
            if (variant == 4) query.filters.insert("token", "PRIVATE");
            if (variant == 5) query.searchText = "other";
            if (variant == 6) query.scope.sourceInstanceId = "kugou/two";
            if (variant == 7) query.filters = {{"playlistId", 8888}};
            session.fetchPage(query); QTRY_COMPARE(failed.size(), variant + 1);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failed.last()[1]).kind, SourceErrorKindV2::InvalidRequest);
        }
        QVERIFY(network.requests.isEmpty());
        session.fetchPage(ranks()); QTRY_COMPARE(network.requests.size(), 1);
        auto bad = catalog(); bad.insert("status", "1"); network.replies[0]->deliver(bad);
        QCOMPARE(failed.size(), 9); QCOMPARE(ready.size(), 0);
        session.fetchPage(rankTracks()); QTRY_COMPARE(network.requests.size(), 2);
        bad = rankTrack(); bad.insert("authors", QJsonArray{QJsonObject{}});
        network.replies[1]->deliver(page({bad}, 1)); QCOMPARE(failed.size(), 10); QCOMPARE(ready.size(), 0);
        QVERIFY(!session.lookupItem(media()));
    }
    void rankCancelCloseAndNotificationsFenceLateResults()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        const auto id = session.fetchPage(ranks()); QTRY_COMPARE(network.requests.size(), 1);
        session.cancel(id); QVERIFY(network.replies[0]->aborted);
        network.replies[0]->deliver(catalog()); QCOMPARE(ready.size(), 0);
        session.fetchPage(rankTracks()); QTRY_COMPARE(network.requests.size(), 2);
        session.close(); QVERIFY(network.replies[1]->aborted); network.replies[1]->deliver(page({rankTrack()}, 1));
        QVERIFY(!session.lookupItem(media())); QCOMPARE(ready.size(), 0);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        connect(&session, &IMusicSourceSessionV2::pageReady, &session, [&session] { session.close(); });
        session.fetchPage(ranks()); QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver(catalog()); QCOMPARE(session.state(), SourceSessionStateV2::Closed);
    }
    void rankProjectionUsesStandardTrackMapperWithoutGrantingUnknownRights()
    {
        for (int variant = 0; variant < 5; ++variant) {
            QJsonObject row{{"hash", hash}, {"songname", "Rank Song"}, {"duration", 60},
                {"authors", QJsonArray{QJsonObject{{"author_name", "Rank Artist"}}}},
                {"url", "https://private/?token=PRIVATE"}, {"token", "PRIVATE"}};
            if (variant == 1) row.insert("pay_type", 0);
            if (variant == 2) row.insert("pay_type", 1);
            if (variant == 3) row.insert("pay_type", "0");
            if (variant == 4) row.insert("pay_type", false);
            const auto projected = KugouRankProtocol::tracks(page({row}, 1), 20); QVERIFY(projected);
            const auto item = KugouMappers::track(projected->rows[0].toObject(),
                {"kugou", "kugou/one", "one", "Kugou"});
            QVERIFY(item); QCOMPARE(item->ref, media());
            QCOMPARE(item->subtitle, "Rank Artist"); QCOMPARE(item->durationMs, 60000);
            QCOMPARE(item->availableActions.value(SourceActionV2::Play).state,
                variant == 1 ? AvailabilityV2::Available : AvailabilityV2::Unavailable);
            QVERIFY(item->metadata.isEmpty()); QVERIFY(item->externalIds.isEmpty());
            QVERIFY(!item->title.contains("PRIVATE"));
        }
    }
    void boundCredentialRecordDoesNotGrantAuthenticationOrLeaveAnonymousTransport()
    {
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto bytes = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_CANDIDATE"}}); QVERIFY(bytes);
        const auto envelope = encodeSourceSecretsV2({{"credentialBundle", *bytes}}); QVERIFY(envelope);
        Network network;
        auto configuration = config(); configuration.secret = *envelope;
        KugouSourceSession session(configuration, &network, nullptr);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(session.identity().accountId, QString("one"));
        QCOMPARE(session.capabilities().accountActions.value(SourceActionV2::Favorite).state, AvailabilityV2::Unsupported);
        QCOMPARE(session.capabilities().accountActions.value(SourceActionV2::Scrobble).state, AvailabilityV2::Unsupported);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 1);
        const auto request = network.requests[0];
        QVERIFY(!request.url().toEncoded().contains("PRIVATE_CANDIDATE"));
        for (const auto &header : request.rawHeaderList()) {
            QVERIFY(!request.rawHeader(header).contains("PRIVATE_CANDIDATE"));
            QVERIFY(header.toLower() != "cookie"); QVERIFY(header.toLower() != "authorization");
        }
        network.replies[0]->deliver(page());
        const auto item = session.lookupItem(media()); QVERIFY(item);
        QVERIFY(item->metadata.isEmpty()); QVERIFY(item->externalIds.isEmpty());
    }
    void credentialConfigurationRejectsForeignCorruptAndLegacyRecordsBeforeTransport()
    {
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto bytes = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_CANDIDATE"}}); QVERIFY(bytes);
        for (int variant = 0; variant < 8; ++variant) {
            Network network; auto configuration = config();
            SourceNamedSecretsV2 secrets{{"credentialBundle", *bytes}};
            if (variant == 0) { configuration.accountId = "two"; configuration.sourceInstanceId = "kugou/two"; }
            if (variant == 1) secrets["credentialBundle"] = "broken";
            if (variant == 2) secrets.insert("extra", "PRIVATE");
            if (variant == 3) secrets = {{"token", "PRIVATE"}};
            if (variant == 4) secrets["credentialBundle"] += '\n';
            if (variant == 5) configuration.parameters.insert("userId", "123");
            configuration.secret = *encodeSourceSecretsV2(secrets);
            if (variant == 6) configuration.secret = *bytes;
            if (variant == 7) { configuration.accountId = " one"; configuration.sourceInstanceId = "kugou/ one"; }
            KugouSourceSession session(configuration, &network, nullptr);
            QSignalSpy failed(&session, &IMusicSourceSessionV2::requestFailed);
            session.open(); QTRY_COMPARE(failed.size(), 1);
            QCOMPARE(session.state(), SourceSessionStateV2::Failed);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failed[0][1]).messageKey, QString("kugou.config.invalid"));
            QVERIFY(network.requests.isEmpty());
        }
    }
    void unknownMediaNeedsExplicitResourceVerificationBeforeIndexing()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy stream(&session, &IMusicSourceSessionV2::streamReady);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        const auto id = session.resolveStream(media());
        QVERIFY(!session.lookupItem(media())); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(verifiedResource());
        QCOMPARE(stream.size(), 1); QCOMPARE(stream[0][0].toUuid(), id);
        const auto item = session.lookupItem(media()); QVERIFY(item);
        QCOMPARE(item->ref, media()); QCOMPARE(item->title, "Verified Song");
        QCOMPARE(item->durationMs, 62000); QCOMPARE(item->subtitle, "Verified Artist");
        QVERIFY(item->metadata.isEmpty()); QVERIFY(item->externalIds.isEmpty());
        KugouSourceSession other(config("two"), &network, nullptr);
        other.open(); QTRY_COMPARE(other.state(), SourceSessionStateV2::Ready);
        QVERIFY(!other.lookupItem(media("two")));
        session.close(); QVERIFY(!session.lookupItem(media()));
    }
    void unknownMediaVerificationFailsClosedForIncompleteOrUnsafeResponses()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy stream(&session, &IMusicSourceSessionV2::streamReady);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        for (int variant = 0; variant < 10; ++variant) {
            if (variant > 0) {
                session.close(); session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
            }
            auto response = verifiedResource(); auto data = response["data"].toObject();
            if (variant == 0) data.remove("pay_type");
            if (variant == 1) data["pay_type"] = "0";
            if (variant == 2) data["pay_type"] = 3;
            if (variant == 3) data["hash"] = QString(32, 'F');
            if (variant == 4) data.remove("songName");
            if (variant == 5) data["songName"] = " ";
            if (variant == 6) data["url"] = "file:///private";
            response["data"] = data;
            if (variant == 7) response.remove("status");
            if (variant == 8) response["status"] = "1";
            if (variant == 9) response["status"] = 0;
            session.resolveStream(media()); QTRY_COMPARE(network.requests.size(), variant + 1);
            network.replies[variant]->deliver(response);
            QCOMPARE(failure.size(), variant + 1); QCOMPARE(stream.size(), 0);
            if (variant == 2) {
                QVERIFY(session.lookupItem(media()));
                QCOMPARE(session.lookupItem(media())->availableActions.value(SourceActionV2::Play).state,
                         AvailabilityV2::Unavailable);
                session.resolveStream(media()); QTRY_COMPARE(failure.size(), variant + 2);
                QCOMPARE(network.requests.size(), variant + 1);
                failure.removeLast();
            } else QVERIFY(!session.lookupItem(media()));
            QVERIFY(qvariant_cast<SourceErrorV2>(failure.last()[1]).detail.isEmpty());
        }
    }
    void verificationCannotUndoRestrictionsAndCancelledRepliesCannotPopulateIndex()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy stream(&session, &IMusicSourceSessionV2::streamReady);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.resolveStream(media()); session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 2);
        network.replies[1]->deliver(page({track(3)}, 1));
        network.replies[0]->deliver(verifiedResource());
        QCOMPARE(failure.size(), 1); QCOMPARE(stream.size(), 0);
        QCOMPARE(session.lookupItem(media())->availableActions.value(SourceActionV2::Play).state,
                 AvailabilityV2::Unavailable);
        session.close(); session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        const auto canceled = session.resolveStream(media()); QTRY_COMPARE(network.requests.size(), 3);
        auto held = network.replies[2]; session.cancel(canceled); QVERIFY(held->aborted);
        held->deliver(verifiedResource()); QVERIFY(!session.lookupItem(media()));
        session.resolveStream(media()); QTRY_COMPARE(network.requests.size(), 4);
        held = network.replies[3]; session.close(); QVERIFY(held->aborted);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        if (held) held->deliver(verifiedResource());
        QVERIFY(!session.lookupItem(media())); QCOMPARE(stream.size(), 0); QCOMPARE(failure.size(), 1);
    }
    void itemLookupIsExactIndexedOnlyAndNeverPerformsIo()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        auto *lookup = qobject_cast<IItemLookupProviderV1 *>(&session);
        QVERIFY(lookup); QVERIFY(!lookup->lookupItem(media()));
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QVERIFY(!lookup->lookupItem(media())); QCOMPARE(network.requests.size(), 0);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page());
        auto item = lookup->lookupItem(media()); QVERIFY(item);
        QCOMPARE(item->title, "Song"); QCOMPARE(item->durationMs, 61500);
        QVERIFY(item->metadata.isEmpty()); QVERIFY(item->externalIds.isEmpty());
        QCOMPARE(item->availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Available);
        // A returned DTO is a copy, not a writable permission handle.
        item->title = "tampered"; item->availableActions.clear();
        QCOMPARE(lookup->lookupItem(media())->title, "Song");
        for (int variant = 0; variant < 7; ++variant) {
            auto ref = media();
            if (variant == 0) ref.sourcePluginId = "local";
            if (variant == 1) ref.sourceInstanceId = "kugou/two";
            if (variant == 2) ref.accountId = "two";
            if (variant == 3) ref.entityType = MediaEntityTypeV2::Album;
            if (variant == 4) ref.entityId = hash.toLower();
            if (variant == 5) ref.entityId = QString(32, 'F');
            if (variant == 6) ref.entityId = "file:///private";
            QVERIFY(!lookup->lookupItem(ref));
        }
        QCOMPARE(network.requests.size(), 1);
        KugouSourceSession other(config("two"), &network, nullptr);
        other.open(); QTRY_COMPARE(other.state(), SourceSessionStateV2::Ready);
        QVERIFY(!other.lookupItem(media("two")));
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 2);
        network.replies[1]->deliver(page({track(3)}, 1));
        QCOMPARE(lookup->lookupItem(media())->availableActions.value(SourceActionV2::Play).state,
                 AvailabilityV2::Unavailable);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver(page());
        QCOMPARE(lookup->lookupItem(media())->availableActions.value(SourceActionV2::Play).state,
                 AvailabilityV2::Unavailable);
        session.close(); QVERIFY(!lookup->lookupItem(media()));
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QVERIFY(!lookup->lookupItem(media())); QCOMPARE(network.requests.size(), 3);
    }
    void defaultSessionOwnsItsNetworkManager()
    {
        auto session = std::make_unique<KugouSourceSession>(config());
        QPointer<QNetworkAccessManager> network = session->findChild<QNetworkAccessManager *>();
        QVERIFY(network); session.reset(); QVERIFY(!network);
    }
    void fullIndexDoesNotEvictRestrictionsOrExposeUnindexedItems()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({track(3)}, 1));
        auto query = search(); query.limit = 100;
        for (int batch = 0; batch < 41; ++batch) {
            QJsonArray rows;
            for (int i = 0; i < 100; ++i)
                rows.append(track(0, QString::number(batch * 100 + i, 16).rightJustified(32, '0').toUpper()));
            session.fetchPage(query); QTRY_COMPARE(network.requests.size(), batch + 2);
            network.replies[batch + 1]->deliver(page(rows, 4100));
        }
        QVERIFY(session.lookupItem(media()));
        QCOMPARE(session.lookupItem(media())->availableActions.value(SourceActionV2::Play).state,
                 AvailabilityV2::Unavailable);
        const auto section = qvariant_cast<PageResultV2>(ready.last()[1]).sections[0];
        const auto overflow = section.items.last();
        QVERIFY(!session.lookupItem(overflow.ref));
        QCOMPARE(overflow.availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.resolveStream(overflow.ref); QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failure[0][1]).kind, SourceErrorKindV2::Authorization);
        QCOMPARE(network.requests.size(), 42);
    }
    void interfacesOpenCapabilitiesAndSearchStreamProtocol()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QVERIFY(qobject_cast<IPageProviderV2 *>(&session)); QVERIFY(qobject_cast<IPlaybackProviderV2 *>(&session));
        QCOMPARE(session.capabilities().action(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        QSignalSpy started(&session, &IMusicSourceSessionV2::requestStarted);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady);
        QSignalSpy stream(&session, &IMusicSourceSessionV2::streamReady);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QCOMPARE(network.requests.size(), 0); // anonymous readiness is local, not service validation
        QCOMPARE(session.capabilities().action(SourceActionV2::Play).state, AvailabilityV2::Available);
        for (auto action : {SourceActionV2::Artwork, SourceActionV2::Lyrics, SourceActionV2::Download,
                            SourceActionV2::Favorite, SourceActionV2::Scrobble})
            QCOMPARE(session.capabilities().action(action).state, AvailabilityV2::Unsupported);
        const auto id = session.fetchPage(search());
        QCOMPARE(started.last()[0].toUuid(), id); QCOMPARE(ready.size(), 0);
        QTRY_COMPARE(network.requests.size(), 1);
        QCOMPARE(QUrlQuery(network.requests[0].url()).queryItemValue("keyword", QUrl::FullyDecoded), "测试 A&B");
        network.replies[0]->deliver(page()); QCOMPARE(ready.size(), 1); QCOMPARE(ready[0][0].toUuid(), id);
        const auto result = qvariant_cast<PageResultV2>(ready[0][1]);
        QCOMPARE(result.sourceStates.value("kugou/one").state, SourcePageLoadStateV2::Ready);
        const auto item = result.sections[0].items[0]; QCOMPARE(item.ref, media()); QCOMPARE(item.durationMs, 61500);
        QVERIFY(item.metadata.isEmpty()); QVERIFY(item.externalIds.isEmpty()); QVERIFY(item.artworkId.isEmpty());
        QCOMPARE(item.availableActions.size(), 1);
        const auto streamId = session.resolveStream(item.ref);
        QCOMPARE(started.last()[0].toUuid(), streamId); QTRY_COMPARE(network.requests.size(), 2);
        QCOMPARE(QUrlQuery(network.requests[1].url()).queryItemValue("hash"), hash);
        network.replies[1]->deliver({{"hash", hash}, {"url", "https://cdn.kugou.com/music.mp3?key=signed"}});
        QCOMPARE(stream.size(), 1); QCOMPARE(stream[0][0].toUuid(), streamId);
        const auto descriptor = qvariant_cast<StreamDescriptorV2>(stream[0][1]); QCOMPARE(descriptor.media, item.ref);
        QCOMPARE(descriptor.url.scheme(), "https"); QVERIFY(descriptor.headers.isEmpty());
    }
    void queryCursorBindsContextAndCloseInvalidatesIt()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady), failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        auto query = search(); session.fetchPage(query); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page()); const auto section = qvariant_cast<PageResultV2>(ready[0][1]).sections[0];
        QVERIFY(section.hasMore); QVERIFY(!section.nextCursor.isEmpty()); query.cursor = section.nextCursor;
        for (int variant = 0; variant < 3; ++variant) {
            auto invalid = query;
            if (variant == 0) invalid.searchText = "other";
            if (variant == 1) invalid.limit = 2;
            if (variant == 2) invalid.cursor = "2";
            session.fetchPage(invalid); QTRY_COMPARE(failure.size(), variant + 1);
        }
        QCOMPARE(network.requests.size(), 1);
        session.fetchPage(query); QTRY_COMPARE(network.requests.size(), 2);
        QCOMPARE(QUrlQuery(network.requests[1].url()).queryItemValue("page"), "2");
        network.replies[1]->deliver(page({}, 2));
        const auto empty = qvariant_cast<PageResultV2>(ready[1][1]);
        QVERIFY(!empty.sections[0].hasMore); QVERIFY(empty.sections[0].nextCursor.isEmpty());
        QCOMPARE(empty.sourceStates.value("kugou/one").state, SourcePageLoadStateV2::Empty);
        session.close(); session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(query); QTRY_COMPARE(failure.size(), 4); QCOMPARE(network.requests.size(), 2);
    }
    void foreignMalformedAndRestrictedMediaNeverRequestAResource()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        for (int variant = 0; variant < 6; ++variant) {
            auto ref = media();
            if (variant == 0) ref.sourcePluginId = "local";
            if (variant == 1) ref.sourceInstanceId = "kugou/two";
            if (variant == 2) ref.accountId = "two";
            if (variant == 3) ref.entityType = MediaEntityTypeV2::Album;
            if (variant == 4) ref.entityId = "file:///x";
            if (variant == 5) ref.entityId = hash.toLower();
            session.resolveStream(ref); QTRY_COMPARE(failure.size(), variant + 1);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failure.last()[1]).kind, SourceErrorKindV2::InvalidRequest);
        }
        QCOMPARE(network.requests.size(), 0);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({track(3)}));
        session.resolveStream(media()); QTRY_COMPARE(failure.size(), 7); QCOMPARE(network.requests.size(), 1);
    }
    void invalidQueriesAndUnsupportedOperationsDoNotUseLegacy()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        for (int variant = 0; variant < 6; ++variant) {
            auto query = search();
            if (variant == 0) query.scope.sourceInstanceId = "kugou/two";
            if (variant == 1) query.limit = 0;
            if (variant == 2) query.limit = 101;
            if (variant == 3) query.searchText = " ";
            if (variant == 4) query.filters.insert("songId", hash);
            if (variant == 5) query.section = PageSectionKindV2::Albums;
            session.fetchPage(query); QTRY_COMPARE(failure.size(), variant + 1);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failure.last()[1]).kind,
                     variant == 5 ? SourceErrorKindV2::Unsupported : SourceErrorKindV2::InvalidRequest);
        }
        PageQueryV2 recommendation; recommendation.scope.sourceInstanceId = "kugou/one";
        session.fetchPage(recommendation); QTRY_COMPARE(failure.size(), 7);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failure.last()[1]).kind, SourceErrorKindV2::Unsupported);
        session.fetchLyrics(media()); session.fetchArtwork(media()); QTRY_COMPARE(failure.size(), 9);
        QCOMPARE(network.requests.size(), 0);
    }
    void cancellationCloseReopenAndMultipleInstances()
    {
        Network network; KugouSourceSession one(config(), &network, nullptr), two(config("two"), &network, nullptr);
        QSignalSpy first(&one, &IMusicSourceSessionV2::pageReady), second(&two, &IMusicSourceSessionV2::pageReady);
        one.open(); two.open(); QTRY_COMPARE(one.state(), SourceSessionStateV2::Ready); QTRY_COMPARE(two.state(), SourceSessionStateV2::Ready);
        const auto id = one.fetchPage(search()); two.fetchPage(search("two")); QTRY_COMPARE(network.requests.size(), 2);
        one.cancel(id); QVERIFY(network.replies[0]->aborted); network.replies[0]->deliver(page());
        network.replies[1]->deliver(page()); QCOMPARE(first.size(), 0); QCOMPARE(second.size(), 1);
        QCOMPARE(qvariant_cast<PageResultV2>(second[0][1]).sections[0].items[0].ref, media("two"));
        one.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 3); auto held = network.replies[2];
        one.close(); QVERIFY(held->aborted); one.open(); QTRY_COMPARE(one.state(), SourceSessionStateV2::Ready);
        if (held) held->deliver(page()); QCOMPARE(first.size(), 0);
        one.resolveStream(media()); QSignalSpy failure(&one, &IMusicSourceSessionV2::requestFailed);
        QTRY_COMPARE(network.requests.size(), 4);
        network.replies[3]->deliver({{"hash", hash}, {"url", "https://cdn.kugou.com/a"}});
        QTRY_COMPARE(failure.size(), 1);
    }
    void malformedBusinessResponsesAreNotSuccessfulEmptyPages()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed), ready(&session, &IMusicSourceSessionV2::pageReady);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        QList<QJsonObject> responses{QJsonObject{{"status", 0}, {"token", "PRIVATE"}}, QJsonObject{}, page()};
        responses[2]["data"] = QJsonObject{{"info", QJsonArray{}}, {"total", -1}};
        for (int i = 0; i < responses.size(); ++i) {
            session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), i + 1);
            network.replies[i]->deliver(responses[i]); QCOMPARE(failure.size(), i + 1);
            QVERIFY(qvariant_cast<SourceErrorV2>(failure.last()[1]).detail.isEmpty());
        }
        QCOMPARE(ready.size(), 0);
    }
    void olderFreeResponseCannotUndoAnObservedRestriction()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady), failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(search()); session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 2);
        network.replies[1]->deliver(page({track(3)}, 1)); network.replies[0]->deliver(page({track(0)}, 1));
        QCOMPARE(ready.size(), 2);
        QCOMPARE(qvariant_cast<PageResultV2>(ready.last()[1]).sections[0].items[0]
                     .availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        session.resolveStream(media()); QTRY_COMPARE(failure.size(), 1); QCOMPARE(network.requests.size(), 2);
    }
    void mapperRejectsWrongHashesUnsafeResourcesAndUnknownRights()
    {
        auto row = track(); row.remove("pay_type");
        const auto item = KugouMappers::track(row, {"kugou", "kugou/one", "one", {}}); QVERIFY(item);
        QCOMPARE(item->availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        row["pay_type"] = "0";
        QCOMPARE(KugouMappers::track(row, {"kugou", "kugou/one", "one", {}})
                     ->availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        for (const auto &url : {"file:///etc/passwd", "data:audio/mp3;base64,x", "/relative", "https://user:secret@cdn.kugou.com/a", "https://cdn.kugou.com/a#fragment"})
            QVERIFY(!KugouMappers::stream({{"hash", hash}, {"url", url}}, media()));
        QVERIFY(!KugouMappers::stream({{"hash", QString(32, 'F')}, {"url", "https://cdn.kugou.com/a"}}, media()));
        QVERIFY(!KugouMappers::stream({{"hash", hash}, {"url", "https://cdn.kugou.com/a"}, {"pay_type", 3}}, media()));
        QVERIFY(KugouMappers::stream({{"data", QJsonObject{{"hash", hash}, {"backup_url", "https://cdn.kugou.com/a"}}}}, media()));
        QVERIFY(!KugouMappers::track(track(0, "not-hash"), {"kugou", "kugou/one", "one", {}}));
    }
    void resourceCompletionRechecksRightsObservedWhileResolving()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy stream(&session, &IMusicSourceSessionV2::streamReady), failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({track(0)}, 1));
        const auto id = session.resolveStream(media()); session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 3);
        network.replies[2]->deliver(page({track(3)}, 1));
        network.replies[1]->deliver({{"hash", hash}, {"url", "https://cdn.kugou.com/a"}});
        QCOMPARE(stream.size(), 0); QCOMPARE(failure.size(), 1); QCOMPARE(failure[0][0].toUuid(), id);
        QCOMPARE(qvariant_cast<SourceErrorV2>(failure[0][1]).kind, SourceErrorKindV2::Authorization);
    }
    void duplicateHashCannotHideARestriction()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy ready(&session, &IMusicSourceSessionV2::pageReady), failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        auto query = search(); query.limit = 2; session.fetchPage(query); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({track(0), track(3)}, 2)); QCOMPARE(ready.size(), 1);
        const auto section = qvariant_cast<PageResultV2>(ready[0][1]).sections[0]; QCOMPARE(section.items.size(), 1);
        QCOMPARE(section.items[0].availableActions.value(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        session.resolveStream(media()); QTRY_COMPARE(failure.size(), 1); QCOMPARE(network.requests.size(), 1);
    }
    void reentrantCloseCancelAndDeletionDuringNotifications()
    {
        Network network;
        KugouSourceSession closed(config(), &network, nullptr);
        connect(&closed, &IMusicSourceSessionV2::requestStarted, &closed, [&closed] { closed.close(); });
        closed.open(); QCoreApplication::processEvents(); QCOMPARE(closed.state(), SourceSessionStateV2::Closed);
        KugouSourceSession connecting(config(), &network, nullptr);
        connect(&connecting, &IMusicSourceSessionV2::stateChanged, &connecting, [&connecting](SourceSessionStateV2 state) {
            if (state == SourceSessionStateV2::Connecting) connecting.close();
        });
        connecting.open(); QCoreApplication::processEvents(); QCOMPARE(connecting.state(), SourceSessionStateV2::Closed);
        QPointer<KugouSourceSession> deleted = new KugouSourceSession(config(), &network, nullptr);
        connect(deleted, &IMusicSourceSessionV2::requestStarted, &network, [&deleted] { delete deleted.data(); });
        deleted->open(); QVERIFY(!deleted);
        KugouSourceSession canceled(config(), &network, nullptr);
        connect(&canceled, &IMusicSourceSessionV2::capabilitiesChanged, &canceled, [&canceled] {
            if (canceled.state() == SourceSessionStateV2::Ready) canceled.close();
        });
        canceled.open(); QCoreApplication::processEvents(); QCOMPARE(canceled.state(), SourceSessionStateV2::Closed);
        QCOMPARE(network.requests.size(), 0);
    }
    void invalidConfigurationAndClosedRequestsFailWithoutNetwork()
    {
        Network network; auto invalid = config(); invalid.secret = "HOST_COOKIE";
        KugouSourceSession session(invalid, &network, nullptr);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.fetchPage(search()); QTRY_COMPARE(failure.size(), 1);
        session.open(); QTRY_COMPARE(failure.size(), 2); QCOMPARE(session.state(), SourceSessionStateV2::Failed);
        QCOMPARE(session.capabilities().action(SourceActionV2::Play).state, AvailabilityV2::Unavailable);
        QVERIFY(qvariant_cast<SourceErrorV2>(failure.last()[1]).detail.isEmpty()); QCOMPARE(network.requests.size(), 0);
    }
};
QTEST_GUILESS_MAIN(KugouSourceSessionTest)
#include "tst_KugouSourceSession.moc"
