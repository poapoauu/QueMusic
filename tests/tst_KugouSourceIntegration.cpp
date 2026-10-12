#include "PlaybackCoordinator.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "HostPluginUiSettingsBridge.h"
#include "../plugins/kugou-source/KugouCredentialRecord.h"
#include "v2/SourceSecretsV2.h"
#include <QFile>
#include "v2/ISourceProvidersV2.h"
#include "extensions/item-lookup/v1/IItemLookupProviderV1.h"
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
class CredentialStore final : public ISecretStore {
public:
    QHash<QString, QByteArray> values;
    bool failWrite = false;
    QString failRemove;
    bool write(const QString &reference, const QByteArray &value, QString *error) override
    {
        if (failWrite) { if (error) *error = "PRIVATE_BACKEND_ERROR"; return false; }
        values.insert(reference, value); return true;
    }
    std::optional<QByteArray> read(const QString &reference, QString *) const override
    { return values.contains(reference) ? std::optional<QByteArray>(values.value(reference)) : std::nullopt; }
    bool remove(const QString &reference, QString *) override
    { if (reference == failRemove) return false; values.remove(reference); return true; }
};
SourceAccountSaveV2 credentialRequest(const QString &account)
{
    // Storage-contract fixture only. Production factory still has empty Schema;
    // this must not become a Host-rendered manual credential input form.
    SettingsFieldV2 field;
    field.id = "credentialBundle"; field.type = SettingsFieldTypeV2::Secret; field.required = true;
    return {"org.quemusic.source.kugou", "kugou", account, "Fixture", true, 1,
        {{"credentialFixture", "fixture", {field}}}, {}};
}
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
    void privateCredentialRecordRejectsIdentityDeviceAndEncodingConflicts()
    {
        using namespace KugouCredentialRecord;
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto bytes = encode({*device, {"9007199254740993", "PRIVATE_TOKEN="}}); QVERIFY(bytes);
        const auto restored = decode(*bytes, "kugou/one", "one", "9007199254740993"); QVERIFY(restored);
        QCOMPARE(restored->device.guid, device->guid); QCOMPARE(restored->device.requestIdentity.mid, device->requestIdentity.mid);
        QVERIFY(!decode(*bytes, "kugou/two", "two")); QVERIFY(!decode(*bytes, "kugou/one", "one", "123"));
        QVERIFY(!decode(*bytes + " ", "kugou/one", "one"));
        QVERIFY(!decode(QByteArray(16385, 'x'), "kugou/one", "one"));
        auto object = QJsonDocument::fromJson(*bytes).object();
        for (const auto &key : {"accountId", "sourceInstanceId", "pluginPackageId", "sourceId", "guid", "dfid", "mid", "userId"}) {
            auto bad = object; bad[key] = "WRONG";
            QVERIFY(!decode(QJsonDocument(bad).toJson(QJsonDocument::Compact), "kugou/one", "one"));
        }
        auto bad = object; bad["version"] = "1";
        QVERIFY(!decode(QJsonDocument(bad).toJson(QJsonDocument::Compact), "kugou/one", "one"));
        bad = object; bad["userId"] = 123;
        QVERIFY(!decode(QJsonDocument(bad).toJson(QJsonDocument::Compact), "kugou/one", "one"));
        bad = object; bad["extra"] = "PRIVATE";
        QVERIFY(!decode(QJsonDocument(bad).toJson(QJsonDocument::Compact), "kugou/one", "one"));
        bad = object; bad["token"] = "x; userid=1";
        QVERIFY(!decode(QJsonDocument(bad).toJson(QJsonDocument::Compact), "kugou/one", "one"));
        QByteArray duplicate = *bytes; duplicate.chop(1); duplicate += ",\"version\":1}";
        QVERIFY(!decode(duplicate, "kugou/one", "one"));
        Account invalid{*device, {"123", "x; userid=1"}}; QVERIFY(!encode(invalid));
    }
    void hostStoresBoundCredentialOnlyInSecureEnvelope()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("accounts.ini"), QSettings::IniFormat);
        CredentialStore secrets; SourceAccountStore store(&settings, &secrets);
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto bytes = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_TOKEN"}}); QVERIFY(bytes);
        HostPluginUiSettingsBridge bridge(&store, nullptr, credentialRequest("one"), QStringList{"credentialBundle"});
        auto *backendSecrets = qobject_cast<IPluginUiBackendSecretsV1 *>(&bridge); QVERIFY(backendSecrets);
        QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
        const auto id = backendSecrets->saveBackendSecret("credentialBundle", *bytes);
        QTRY_COMPARE(finished.size(), 1); QCOMPARE(finished[0][0].toUuid(), id); QVERIFY(finished[0][1].toBool());
        QVERIFY(bridge.publicValues().isEmpty()); QVERIFY(bridge.sections().isEmpty());
        QVERIFY(!bridge.secretConfigured("credentialBundle")); QVERIFY(backendSecrets->backendSecretConfigured("credentialBundle"));
        const auto fromService = backendSecrets->readBackendSecret("credentialBundle"); QVERIFY(fromService);
        QCOMPARE(*fromService, *bytes);
        QVERIFY(KugouCredentialRecord::decode(*fromService, "kugou/one", "one", "123"));
        QVERIFY(!QJsonDocument::fromVariant(bridge.sections()).toJson().contains("PRIVATE"));
        const auto record = store.storedAccount("kugou", "one"); QVERIFY(record);
        QCOMPARE(record->pluginPackageId, packageId); QCOMPARE(record->sourceInstanceId, "kugou/one");
        QVERIFY(record->parameters.isEmpty()); QCOMPARE(record->secretFormat, "namedEnvelopeV2");
        const auto fields = decodeSourceSecretsV2(secrets.values.value(record->secretReference)); QVERIFY(fields);
        QCOMPARE(fields->keys(), QStringList{"credentialBundle"}); QCOMPARE(fields->value("credentialBundle"), *bytes);
        settings.sync(); QFile metadata(settings.fileName()); QVERIFY(metadata.open(QIODevice::ReadOnly));
        const auto ini = metadata.readAll(); metadata.close(); QVERIFY(!ini.contains("PRIVATE"));
        QVERIFY(!ini.contains(device->guid.toUtf8())); QVERIFY(!ini.contains(device->requestIdentity.dfid.toUtf8()));
        QSettings reopened(settings.fileName(), QSettings::IniFormat); SourceAccountStore restored(&reopened, &secrets);
        const auto resolved = restored.resolvedAccountV2("kugou", "one"); QVERIFY(resolved);
        const auto envelope = decodeSourceSecretsV2(resolved->secret); QVERIFY(envelope);
        QVERIFY(KugouCredentialRecord::decode(envelope->value("credentialBundle"), "kugou/one", "one", "123"));
        QVERIFY(!KugouCredentialRecord::decode(envelope->value("credentialBundle"), "kugou/two", "two"));
        auto foreign = credentialRequest("one"); foreign.pluginPackageId = "org.example.foreign";
        HostPluginUiSettingsBridge conflict(&store, nullptr, foreign);
        QSignalSpy rejected(&conflict, &PluginUiSettingsBridge::operationFinished);
        conflict.saveSecret("credentialBundle", "PRIVATE_HIJACK"); QTRY_COMPARE(rejected.size(), 1);
        QVERIFY(!rejected[0][1].toBool()); QCOMPARE(secrets.values.size(), 1);
        backendSecrets->clearBackendSecret("credentialBundle"); QTRY_COMPARE(finished.size(), 2);
        QVERIFY(finished[1][1].toBool()); QVERIFY(!backendSecrets->backendSecretConfigured("credentialBundle")); QVERIFY(secrets.values.isEmpty());
    }
    void hostCredentialRotationFailurePreservesExistingBinding()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("accounts.ini"), QSettings::IniFormat);
        CredentialStore secrets; SourceAccountStore store(&settings, &secrets);
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto old = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_OLD"}}); QVERIFY(old);
        const auto next = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_NEW"}}); QVERIFY(next);
        HostPluginUiSettingsBridge bridge(&store, nullptr, credentialRequest("one"));
        QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
        bridge.saveSecret("credentialBundle", QString::fromUtf8(*old)); QTRY_COMPARE(finished.size(), 1); QVERIFY(finished[0][1].toBool());
        const auto reference = store.secretReference("kugou", "one");
        secrets.failWrite = true;
        bridge.saveSecret("credentialBundle", QString::fromUtf8(*next)); QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!finished[1][1].toBool()); QCOMPARE(finished[1][2].toString(), "source.settings.secureWriteFailed");
        QCOMPARE(store.secretReference("kugou", "one"), reference); QCOMPARE(secrets.values.size(), 1);
        secrets.failWrite = false; secrets.failRemove = reference;
        bridge.saveSecret("credentialBundle", QString::fromUtf8(*next)); QTRY_COMPARE(finished.size(), 3);
        QVERIFY(!finished[2][1].toBool()); QCOMPARE(finished[2][2].toString(), "source.settings.previousSecretCleanupFailed");
        QCOMPARE(store.secretReference("kugou", "one"), reference); QCOMPARE(secrets.values.size(), 1);
        QCOMPARE(decodeSourceSecretsV2(secrets.values.value(reference))->value("credentialBundle"), *old);
        bridge.invalidate(); bridge.saveSecret("credentialBundle", QString::fromUtf8(*next));
        QCoreApplication::processEvents(); QCOMPARE(finished.size(), 3); QCOMPARE(secrets.values.size(), 1);
    }
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
