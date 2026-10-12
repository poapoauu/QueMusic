#include "PlaybackCoordinator.h"
#include "SourceAccountStore.h"
#include "PluginManager.h"
#include "SourceRegistry.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include "extensions/item-lookup/v1/IItemLookupProviderV1.h"
#include "plugin-ui/v1/IPluginUiBackendSecretProviderV1.h"
#include "SettingsSchemaPresentation.h"
#include "PluginSettingsController.h"
#include "KugouCredentialRecord.h"
#include <QNetworkAccessManager>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString packageId = "org.quemusic.source.kugou";
struct Secrets final : ISecretStore {
    QHash<QString, QByteArray> values;
    bool write(const QString &id, const QByteArray &bytes, QString *) override { values[id] = bytes; return true; }
    std::optional<QByteArray> read(const QString &id, QString *) const override
    { return values.contains(id) ? std::optional<QByteArray>(values.value(id)) : std::nullopt; }
    bool remove(const QString &id, QString *) override { values.remove(id); return true; }
};
struct Sink final : PlaybackSink {
    int prepares = 0;
    int plays = 0;
    bool prepare(StreamDescriptorV2, QUuid) override { ++prepares; return true; }
    void play(QUuid) override { ++plays; }
    void stop(QUuid) override {}
};
struct Harness {
    QTemporaryDir storage;
    QSettings settings{storage.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore accounts{&settings, &secrets};
    PluginManager plugins;
    SourceRegistry registry{&plugins, &accounts};
    Sink sink;
    PlaybackCoordinator coordinator{&registry, &sink};
    bool load()
    {
        plugins.addSearchPath(QStringLiteral(QUEMUSIC_KUGOU_PACKAGE_ROOT));
        plugins.discover();
        return plugins.load(packageId);
    }
    bool save(const QString &account)
    {
        auto *provider = qobject_cast<IPluginSettingsProviderV2 *>(plugins.pluginInstance(packageId));
        if (!provider) return false;
        SourceAccountSaveV2 request;
        request.pluginPackageId = packageId; request.sourceId = "kugou";
        request.accountId = account; request.displayName = "Anonymous " + account;
        request.enabled = true; request.schema = provider->settingsSchema();
        return accounts.saveValidatedV2(request);
    }
};
MediaRefV2 media()
{
    return {"kugou", "kugou/one", "one", MediaEntityTypeV2::Track,
            "0123456789ABCDEF0123456789ABCDEF"};
}
}

class KugouSourceRegistrationTest final : public QObject {
    Q_OBJECT
private slots:
    void realHostFormHidesCredentialAndPreservesBoundRecord()
    {
        Harness h; QVERIFY(h.load());
        auto *provider = qobject_cast<IPluginSettingsProviderV2 *>(h.plugins.pluginInstance(packageId)); QVERIFY(provider);
        const auto device = KugouQrProtocol::createDevice("kugou/one", "one"); QVERIFY(device);
        const auto record = KugouCredentialRecord::encode({*device, {"123", "PRIVATE_CANDIDATE"}}); QVERIFY(record);
        QVERIFY(h.accounts.saveValidatedV2({packageId, "kugou", "one", "Candidate", true, 1,
            provider->settingsSchema(), {{"credentialBundle", QString::fromUtf8(*record)}}}));
        PluginSettingsController controller(&h.plugins, &h.registry, &h.accounts);
        QVERIFY(controller.selectPlugin(packageId));
        QVERIFY(controller.settingsSections().isEmpty());
        QVERIFY(controller.selectInstance("kugou/one"));
        QVERIFY(controller.settingsSections().isEmpty());
        for (const auto &entry : controller.instances())
            QVERIFY(entry.toMap().value("configuredSecretFieldIds").toStringList().isEmpty());
        const auto saved = h.accounts.resolvedAccountV2("kugou", "one"); QVERIFY(saved);
        QVERIFY(!controller.saveInstance("Forged", {{"credentialBundle", "PRIVATE_FORGED"}}));
        QVERIFY(controller.saveInstance("Renamed", {}));
        const auto preserved = h.accounts.resolvedAccountV2("kugou", "one"); QVERIFY(preserved);
        QCOMPARE(preserved->secret, saved->secret);
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        QCOMPARE(session->capabilities().accountActions.value(SourceActionV2::Favorite).state, AvailabilityV2::Unsupported);
        QVERIFY(h.save("two"));
        auto *second = h.registry.sessionFor("kugou/two"); QVERIFY(second);
        QTRY_COMPARE(second->state(), SourceSessionStateV2::Ready);
        QVERIFY(h.accounts.resolvedAccountV2("kugou", "two")->secret.isEmpty());
        h.settings.sync();
        QFile file(h.storage.filePath("accounts.ini")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto ini = file.readAll(); QVERIFY(!ini.contains("PRIVATE_CANDIDATE")); QVERIFY(!ini.contains(device->guid.toUtf8()));
        h.registry.closeAll();
    }
    void realModuleFactoryAndRegistryCreateIsolatedInstances()
    {
        Harness h; QVERIFY(h.load());
        auto *root = h.plugins.pluginInstance(packageId);
        auto *plugin = qobject_cast<IMusicSourcePluginV2 *>(root);
        auto *settings = qobject_cast<IPluginSettingsProviderV2 *>(root);
        QVERIFY(plugin); QVERIFY(settings);
        QCOMPARE(root->property("sourceSdkAbi").toInt(), 2);
        const auto descriptor = plugin->descriptor();
        QCOMPARE(descriptor.pluginPackageId, packageId); QCOMPARE(descriptor.sourceId, "kugou");
        QCOMPARE(descriptor.version, "0.1.0"); QCOMPARE(descriptor.sdkAbi, 2);
        QCOMPARE(descriptor.declaredActions.size(), 1);
        QCOMPARE(descriptor.declaredActions.value(SourceActionV2::Play).state, AvailabilityV2::Available);
        auto *backendSecrets = qobject_cast<IPluginUiBackendSecretProviderV1 *>(root);
        QVERIFY(backendSecrets);
        QCOMPARE(backendSecrets->backendSecretFieldIds(), QStringList{"credentialBundle"});
        const auto schema = settings->settingsSchema();
        QCOMPARE(schema.size(), 1); QCOMPARE(schema[0].fields.size(), 1);
        QCOMPARE(schema[0].fields[0].type, SettingsFieldTypeV2::Secret);
        QVERIFY(!schema[0].fields[0].required);
        const auto presentation = backendSecretPresentationV1(schema, backendSecrets->backendSecretFieldIds());
        QVERIFY(presentation); QVERIFY(presentation->schema.isEmpty());
        QVERIFY(h.registry.enabledInstances().isEmpty());
        QVERIFY(h.save("one")); QVERIFY(h.save("two"));
        auto *first = h.registry.sessionFor("kugou/one");
        auto *second = h.registry.sessionFor("kugou/two");
        QVERIFY(first && second && first != second);
        QTRY_COMPARE(first->state(), SourceSessionStateV2::Ready);
        QTRY_COMPARE(second->state(), SourceSessionStateV2::Ready);
        QCOMPARE(first->identity().sourceInstanceId, "kugou/one");
        QCOMPARE(second->identity().accountId, "two");
        auto *oneNetwork = first->findChild<QNetworkAccessManager *>();
        auto *twoNetwork = second->findChild<QNetworkAccessManager *>();
        QVERIFY(oneNetwork && twoNetwork && oneNetwork != twoNetwork);
        QVERIFY(qobject_cast<IPageProviderV2 *>(first));
        QVERIFY(qobject_cast<IPlaybackProviderV2 *>(first));
        auto *lookup = qobject_cast<IItemLookupProviderV1 *>(first); QVERIFY(lookup);
        QVERIFY(!lookup->lookupItem(media()));
        QPointer<IMusicSourceSessionV2> retired = first;
        QVERIFY(h.registry.disableInstance("kugou/one"));
        QTRY_VERIFY(!retired);
        QVERIFY(!h.registry.sessionFor("kugou/one"));
        QCOMPARE(second->state(), SourceSessionStateV2::Ready);
        QVERIFY(h.registry.enableInstance("kugou/one"));
        auto *reopened = h.registry.sessionFor("kugou/one"); QVERIFY(reopened);
        QTRY_COMPARE(reopened->state(), SourceSessionStateV2::Ready);
        QVERIFY(!qobject_cast<IItemLookupProviderV1 *>(reopened)->lookupItem(media()));
        h.registry.closeAll();
    }
    void realFactoryRejectsHostSecretsAndUnknownParameters()
    {
        Harness h; QVERIFY(h.load());
        auto *factory = qobject_cast<IMusicSourcePluginV2 *>(h.plugins.pluginInstance(packageId));
        QVERIFY(factory);
        for (int variant = 0; variant < 2; ++variant) {
            SourceConfigurationV2 config{packageId, "kugou", "kugou/one", "one", "Anonymous", {}, {}};
            if (variant == 0) config.secret = "HOST_COOKIE";
            else config.parameters.insert("serverUrl", "https://unexpected.invalid");
            QObject owner;
            auto *session = factory->createSession(config, &owner); QVERIFY(session);
            QSignalSpy failure(session, &IMusicSourceSessionV2::requestFailed);
            session->open(); QTRY_COMPARE(failure.size(), 1);
            QCOMPARE(session->state(), SourceSessionStateV2::Failed);
            QCOMPARE(qvariant_cast<SourceErrorV2>(failure[0][1]).kind, SourceErrorKindV2::InvalidRequest);
        }
    }
    void coordinatorRejectsUntrustedRestoreAndForeignIdentityWithoutTransport()
    {
        Harness h; QVERIFY(h.load()); QVERIFY(h.save("one"));
        auto *session = h.registry.sessionFor("kugou/one"); QVERIFY(session);
        QTRY_COMPARE(session->state(), SourceSessionStateV2::Ready);
        auto *network = session->findChild<QNetworkAccessManager *>(); QVERIFY(network);
        QSignalSpy transport(network, &QNetworkAccessManager::finished);
        QSignalSpy started(session, &IMusicSourceSessionV2::requestStarted);
        QueueOccurrence occurrence;
        occurrence.occurrenceId = QUuid::createUuid(); occurrence.ref = media();
        occurrence.title = "Historical display only"; occurrence.playableAtEnqueue = false;
        QVERIFY(h.coordinator.restoreQueue({occurrence}));
        QSignalSpy failure(&h.coordinator, &PlaybackCoordinator::playbackFailed);
        h.coordinator.playQueueEntry(0); QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(started.size(), 0); QCOMPARE(h.sink.prepares, 0); QCOMPARE(h.sink.plays, 0);
        occurrence.ref.accountId = "foreign"; occurrence.playableAtEnqueue = true;
        QVERIFY(h.coordinator.restoreQueue({occurrence}));
        h.coordinator.playQueueEntry(0); QTRY_COMPARE(failure.size(), 2);
        QCOMPARE(started.size(), 0); QCOMPARE(transport.size(), 0);
        QCOMPARE(h.sink.prepares, 0); QCOMPARE(h.sink.plays, 0);
        QVERIFY(h.registry.disableInstance("kugou/one"));
        occurrence.ref = media(); QVERIFY(h.coordinator.restoreQueue({occurrence}));
        h.coordinator.playQueueEntry(0); QTRY_COMPARE(failure.size(), 3);
        QCOMPARE(h.sink.prepares, 0); QCOMPARE(h.sink.plays, 0);
    }
};
QTEST_GUILESS_MAIN(KugouSourceRegistrationTest)
#include "tst_KugouSourceRegistration.moc"
