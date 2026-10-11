#include "KugouSourceSession.h"
#include "KugouMappers.h"
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
MediaRefV2 media(const QString &account = "one", const QString &id = hash)
{
    return {"kugou", "kugou/" + account, account, MediaEntityTypeV2::Track, id};
}
}

class KugouSourceSessionTest final : public QObject {
    Q_OBJECT
private slots:
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
    void foreignMalformedAndUnverifiedMediaNeverRequestAResource()
    {
        Network network; KugouSourceSession session(config(), &network, nullptr);
        QSignalSpy failure(&session, &IMusicSourceSessionV2::requestFailed);
        session.open(); QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
        for (int variant = 0; variant < 7; ++variant) {
            auto ref = media();
            if (variant == 0) ref.sourcePluginId = "local";
            if (variant == 1) ref.sourceInstanceId = "kugou/two";
            if (variant == 2) ref.accountId = "two";
            if (variant == 3) ref.entityType = MediaEntityTypeV2::Album;
            if (variant == 4) ref.entityId = "file:///x";
            if (variant == 5) ref.entityId = hash.toLower();
            session.resolveStream(ref); QTRY_COMPARE(failure.size(), variant + 1);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failure.last()[1]).kind,
                     variant == 6 ? SourceErrorKindV2::Authorization : SourceErrorKindV2::InvalidRequest);
        }
        QCOMPARE(network.requests.size(), 0);
        session.fetchPage(search()); QTRY_COMPARE(network.requests.size(), 1);
        network.replies[0]->deliver(page({track(3)}));
        session.resolveStream(media()); QTRY_COMPARE(failure.size(), 8); QCOMPARE(network.requests.size(), 1);
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
        QTRY_COMPARE(failure.size(), 1); QCOMPARE(network.requests.size(), 3);
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
