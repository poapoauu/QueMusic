#include "PlaybackCoordinator.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "v2/ISourceProvidersV2.h"
#include "extensions/item-lookup/v1/IItemLookupProviderV1.h"
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString packageId = "org.quemusic.source.kugou";
MediaRefV2 media(const QString &account = "one")
{
    return {"kugou", "kugou/" + account, account, MediaEntityTypeV2::Track,
            "0123456789ABCDEF0123456789ABCDEF"};
}
struct Sink final : PlaybackSink {
    int prepares = 0, plays = 0, stops = 0;
    bool accepts = true;
    StreamDescriptorV2 stream;
    bool prepare(StreamDescriptorV2 descriptor, QUuid) override
    {
        ++prepares; stream = std::move(descriptor); return accepts;
    }
    void play(QUuid) override { ++plays; }
    void stop(QUuid) override { ++stops; }
};
struct Harness {
    QTemporaryDir storage;
    QSettings settings{storage.filePath("accounts.ini"), QSettings::IniFormat};
    UnavailableSecretStore secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    Sink sink;
    PlaybackCoordinator coordinator{&registry, &sink};
    QPointer<QObject> control;
    bool load()
    {
        plugins.addSearchPath(QStringLiteral(QUEMUSIC_KUGOU_FIXTURE_ROOT)); plugins.discover();
        if (!plugins.load(packageId)) return false;
        control = plugins.pluginInstance(packageId); return bool(control);
    }
    bool save(const QString &account)
    {
        auto *provider = qobject_cast<IPluginSettingsProviderV2 *>(control.data());
        if (!provider) return false;
        SourceAccountSaveV2 request;
        request.pluginPackageId = packageId; request.sourceId = "kugou"; request.accountId = account;
        request.displayName = "Fixture " + account; request.enabled = true;
        request.schema = provider->settingsSchema(); return accounts.saveValidatedV2(request);
    }
    bool restore(const QString &account = "one")
    {
        QueueOccurrence item;
        item.occurrenceId = QUuid::createUuid(); item.ref = media(account);
        item.title = "Historical display only"; item.playableAtEnqueue = true;
        return coordinator.restoreQueue({item});
    }
    bool complete() { return QMetaObject::invokeMethod(control.data(), "completePending", Qt::DirectConnection); }
};
PageQueryV2 search(const QString &account = "one")
{
    PageQueryV2 query; query.page = MusicPageKindV2::Search; query.section = PageSectionKindV2::Tracks;
    query.scope.sourceInstanceId = "kugou/" + account; query.searchText = "fixture"; query.limit = 1;
    return query;
}
QVariantMap row(const MediaItemV2 &item)
{
    return {{"ref", mediaRefV2ToVariantMap(item.ref)}, {"title", item.title}, {"durationMs", item.durationMs},
        {"availableActions", QVariantMap{{QString::number(int(SourceActionV2::Play)),
            QVariantMap{{"state", int(item.availableActions.value(SourceActionV2::Play).state)},
                {"constraints", QVariantMap{}}}}}}};
}
}
class KugouSourceIntegrationTest final : public QObject {
    Q_OBJECT
private slots:
    void searchToCoordinatorToSinkKeepsResourceOutOfPublicState()
    {
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one"));
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
        qobject_cast<IPageProviderV2 *>(session)->fetchPage(search()); QTRY_COMPARE(ready.size(), 1);
        const auto item = qvariant_cast<PageResultV2>(ready[0][1]).sections[0].items[0];
        QCOMPARE(item.ref, media()); QVERIFY(item.metadata.isEmpty());
        QSignalSpy playing(&h.coordinator, &PlaybackCoordinator::playbackStarted);
        const auto generation = h.coordinator.play(row(item)); QVERIFY(!generation.isNull());
        QTRY_COMPARE(playing.size(), 1); QCOMPARE(playing[0][0].toUuid(), generation);
        QCOMPARE(h.sink.prepares, 1); QCOMPARE(h.sink.plays, 1); QCOMPARE(h.sink.stream.media, media());
        QVERIFY(h.sink.stream.url.toString().contains("PRIVATE"));
        QCOMPARE(h.control->property("resourceCount").toInt(), 1);
        const auto publicState = QJsonDocument::fromVariant(QVariantMap{{"queue", h.coordinator.queue()},
            {"current", h.coordinator.currentItem()}}).toJson();
        QVERIFY(!publicState.contains("PRIVATE")); QVERIFY(!publicState.contains("cdn.kugou.com"));
        QVERIFY(h.coordinator.stop()); QCOMPARE(h.sink.stops, 1);
    }
    void restoredPlaybackVerifiesResourceInsteadOfTrustingSnapshot_data()
    {
        QTest::addColumn<QString>("fault"); QTest::addColumn<bool>("success");
        QTest::newRow("explicit-free") << QString{} << true;
        QTest::newRow("missing-rights") << QString("missingRights") << false;
        QTest::newRow("paid") << QString("paid") << false;
        QTest::newRow("unsafe-url") << QString("unsafeUrl") << false;
        QTest::newRow("wrong-hash") << QString("wrongHash") << false;
    }
    void restoredPlaybackVerifiesResourceInsteadOfTrustingSnapshot()
    {
        QFETCH(QString, fault); QFETCH(bool, success);
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one"));
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        auto *lookup = qobject_cast<IItemLookupProviderV1 *>(session); QVERIFY(lookup);
        QVERIFY(!lookup->lookupItem(media()));
        if (fault == "paid") h.control->setProperty("pay", 3);
        else if (!fault.isEmpty()) h.control->setProperty(fault.toLatin1().constData(), true);
        QVERIFY(h.restore()); QCoreApplication::processEvents();
        QCOMPARE(h.control->property("resourceCount").toInt(), 0); QCOMPARE(h.sink.plays, 0);
        QSignalSpy playing(&h.coordinator, &PlaybackCoordinator::playbackStarted);
        QSignalSpy failure(&h.coordinator, &PlaybackCoordinator::playbackFailed);
        h.coordinator.playQueueEntry(0);
        if (success) {
            QTRY_COMPARE(playing.size(), 1); QCOMPARE(failure.size(), 0);
            QCOMPARE(h.sink.prepares, 1); QCOMPARE(h.sink.plays, 1);
            QVERIFY(lookup->lookupItem(media())); QCOMPARE(lookup->lookupItem(media())->title, "Verified Fixture Song");
        } else {
            QTRY_COMPARE(failure.size(), 1); QCOMPARE(playing.size(), 0);
            QCOMPARE(h.sink.prepares, 0); QCOMPARE(h.sink.plays, 0);
            const auto item = lookup->lookupItem(media());
            QVERIFY(!item || item->availableActions.value(SourceActionV2::Play).state == AvailabilityV2::Unavailable);
        }
        QCOMPARE(h.control->property("resourceCount").toInt(), 1);
    }
    void rapidInstanceSwitchCancelsOldVerification()
    {
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one")); QVERIFY(h.save("two"));
        auto *one = h.registry.sessionFor("kugou/one"), *two = h.registry.sessionFor("kugou/two");
        QVERIFY(one && two); QTRY_COMPARE(one->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(two->state(), SourceSessionStateV2::Ready);
        h.control->setProperty("holdResources", true);
        QVERIFY(h.restore()); h.coordinator.playQueueEntry(0);
        QTRY_COMPARE(h.control->property("resourceCount").toInt(), 1);
        MediaItemV2 item; item.ref = media("two"); item.title = "second";
        item.availableActions.insert(SourceActionV2::Play, {AvailabilityV2::Available, {}, {}});
        const auto generation = h.coordinator.play(row(item));
        QTRY_COMPARE(h.control->property("resourceCount").toInt(), 2);
        QSignalSpy playing(&h.coordinator, &PlaybackCoordinator::playbackStarted);
        QVERIFY(h.complete()); QTRY_COMPARE(playing.size(), 1);
        QCOMPARE(playing[0][0].toUuid(), generation); QCOMPARE(h.sink.plays, 1);
        QCOMPARE(h.sink.stream.media, media("two"));
        QVERIFY(!qobject_cast<IItemLookupProviderV1 *>(one)->lookupItem(media()));
        QVERIFY(qobject_cast<IItemLookupProviderV1 *>(two)->lookupItem(media("two")));
    }
    void permissionDowngradeWhileVerificationIsPendingPreventsPlayback()
    {
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one"));
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        h.control->setProperty("holdResources", true); QVERIFY(h.restore());
        QSignalSpy failure(&h.coordinator, &PlaybackCoordinator::playbackFailed);
        h.coordinator.playQueueEntry(0); QTRY_COMPARE(h.control->property("resourceCount").toInt(), 1);
        h.control->setProperty("pay", 3);
        QSignalSpy ready(session, &IMusicSourceSessionV2::pageReady);
        qobject_cast<IPageProviderV2 *>(session)->fetchPage(search()); QTRY_COMPARE(ready.size(), 1);
        QVERIFY(h.complete()); QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(h.sink.prepares, 0); QCOMPARE(h.sink.plays, 0);
    }
    void stopAndDisableFencePendingResourceAndAllowSafeUnload_data()
    {
        QTest::addColumn<bool>("disable");
        QTest::newRow("stop") << false; QTest::newRow("disable") << true;
    }
    void stopAndDisableFencePendingResourceAndAllowSafeUnload()
    {
        QFETCH(bool, disable);
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one"));
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        h.control->setProperty("holdResources", true); QVERIFY(h.restore());
        h.coordinator.playQueueEntry(0); QTRY_COMPARE(h.control->property("resourceCount").toInt(), 1);
        QCOMPARE(h.plugins.unload(packageId), PluginOperationResult::Busy);
        if (disable) QVERIFY(h.registry.disableInstance("kugou/one"));
        else QVERIFY(h.coordinator.stop());
        QVERIFY(h.complete()); QCoreApplication::processEvents();
        QCOMPARE(h.sink.prepares, 0); QCOMPARE(h.sink.plays, 0);
        h.registry.closeAll(); QTRY_COMPARE(h.plugins.plugin(packageId).activeLeases, 0);
        QCOMPARE(h.plugins.unload(packageId), PluginOperationResult::Success);
        QVERIFY(!h.control);
    }
    void sinkPrepareFailureNeverStartsPlayback()
    {
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one"));
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        h.sink.accepts = false; QVERIFY(h.restore());
        QSignalSpy failure(&h.coordinator, &PlaybackCoordinator::playbackFailed);
        h.coordinator.playQueueEntry(0); QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(h.sink.prepares, 1); QCOMPARE(h.sink.plays, 0);
    }
};
QTEST_GUILESS_MAIN(KugouSourceIntegrationTest)
#include "tst_KugouSourceIntegration.moc"
