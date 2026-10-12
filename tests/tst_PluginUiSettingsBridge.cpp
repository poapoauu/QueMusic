#include "HostPluginUiSettingsBridge.h"
#include "SourceAccountStore.h"
#include "v2/SourceSecretsV2.h"

#include <QSettings>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
class Secrets final : public ISecretStore {
public:
    QHash<QString, QByteArray> values;
    bool write(const QString &reference, const QByteArray &bytes, QString *) override
    { values.insert(reference, bytes); return true; }
    std::optional<QByteArray> read(const QString &reference, QString *) const override
    { return values.contains(reference) ? std::optional<QByteArray>(values.value(reference)) : std::nullopt; }
    bool remove(const QString &reference, QString *) override
    { values.remove(reference); return true; }
};
SettingsSchemaV2 schema()
{
    SettingsFieldV2 server;
    server.id = "server";
    server.type = SettingsFieldTypeV2::Text;
    SettingsFieldV2 password;
    password.id = "password";
    password.type = SettingsFieldTypeV2::Secret;
    return {{"main", "settings.main", {server, password}}};
}
struct Fixture {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore store{&settings, &secrets};
    SourceAccountSaveV2 request{"org.example.plugin", "example", "home", "Home", true, 1,
                                schema(), {{"server", "original"}, {"password", "PRIVATE-password"}}};
    Fixture() { store.saveValidatedV2(request); }
};
}

class PluginUiSettingsBridgeTest final : public QObject {
    Q_OBJECT
private slots:
    void publicValuesOmitSecrets();
    void rejectsSecretFieldOutsideSchema();
    void secretOperationsNeverExposeSecretValues();
    void bindsWritesToCurrentInstance();
    void directoryRequestReturnsOnlyLocalUrl();
    void invalidatedServicesIgnoreLateResults();
    void notificationDoesNotIncludeSecretData();
    void createsRequiredPublicAndSecretFieldsAtomically();
    void exposesOnlyPublicVisibilityCondition();
    void backendSecretsAreCppOnlyAndOmittedFromPresentation();
    void backendSecretDeclarationAndRecordsFailClosed();
    void backendSecretsRemainInstanceBoundAndInvalidatable();
    void backendNotificationMayDestroyService();
};

void PluginUiSettingsBridgeTest::publicValuesOmitSecrets()
{
    Fixture f;
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request);
    QCOMPARE(bridge.publicValues().value("server").toString(), QString("original"));
    QVERIFY(!bridge.publicValues().contains("password"));
    QVERIFY(bridge.secretConfigured("password"));
    const auto fields = bridge.sections().first().toMap().value("fields").toList();
    const auto secret = fields.last().toMap();
    QVERIFY(!secret.contains("value"));
    QVERIFY(secret.value("credentialConfigured").toBool());
}

void PluginUiSettingsBridgeTest::rejectsSecretFieldOutsideSchema()
{
    Fixture f;
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request);
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    const auto id = bridge.saveSecret("server", "PRIVATE-invalid");
    QVERIFY(!id.isNull());
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(0).toUuid(), id);
    QVERIFY(!finished.first().at(1).toBool());
    QCOMPARE(finished.first().at(2).toString(), QString("source.settings.invalidSecretDraft"));
    QCOMPARE(f.store.storedAccount("example", "home")->parameters.value("server"), QVariant("original"));
}

void PluginUiSettingsBridgeTest::secretOperationsNeverExposeSecretValues()
{
    Fixture f;
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request);
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    bridge.saveSecret("password", "PRIVATE-replacement");
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished.first().at(1).toBool());
    QVERIFY(!QString::fromUtf8(QJsonDocument::fromVariant(bridge.sections()).toJson()).contains("PRIVATE"));
    QVERIFY(!QString::fromUtf8(QJsonDocument::fromVariant(bridge.publicValues()).toJson()).contains("PRIVATE"));
    QVERIFY(!finished.first().at(2).toString().contains("PRIVATE"));
    const auto ref = f.store.secretReference("example", "home");
    QCOMPARE(decodeSourceSecretsV2(f.secrets.values.value(ref))->value("password"), QByteArray("PRIVATE-replacement"));
    bridge.clearSecret("password");
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(finished.last().at(1).toBool());
    QVERIFY(!bridge.secretConfigured("password"));
}

void PluginUiSettingsBridgeTest::bindsWritesToCurrentInstance()
{
    Fixture f;
    auto foreign = f.request;
    foreign.accountId = "other";
    foreign.pluginPackageId = "org.example.foreign";
    QVERIFY(f.store.saveValidatedV2(foreign));
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request);
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    bridge.savePublicValues({{"server", "changed"}});
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished.first().at(1).toBool());
    QCOMPARE(f.store.storedAccount("example", "home")->parameters.value("server"), QVariant("changed"));
    QCOMPARE(f.store.storedAccount("example", "other")->parameters.value("server"), QVariant("original"));
    auto hijacked = f.request;
    hijacked.pluginPackageId = "org.example.foreign";
    HostPluginUiSettingsBridge bad(&f.store, nullptr, hijacked);
    QSignalSpy rejected(&bad, &PluginUiSettingsBridge::operationFinished);
    bad.saveSecret("password", "PRIVATE-hijack");
    QTRY_COMPARE(rejected.size(), 1);
    QVERIFY(!rejected.first().at(1).toBool());
    QCOMPARE(rejected.first().at(2).toString(), QString("source.settings.identityConflict"));
}

void PluginUiSettingsBridgeTest::directoryRequestReturnsOnlyLocalUrl()
{
    HostPluginUiHostServices host;
    QSignalSpy requested(&host, &PluginUiHostServices::directoryRequested);
    QSignalSpy selected(&host, &PluginUiHostServices::directorySelected);
    const QUuid id = host.requestDirectory();
    QCOMPARE(requested.size(), 1);
    QCOMPARE(requested.first().first().toUuid(), id);
    host.completeDirectory(id, QUrl("https://example.invalid/private"));
    QCOMPARE(selected.size(), 0);
    host.completeDirectory(id, QUrl::fromLocalFile("/tmp/music"));
    QCOMPARE(selected.size(), 0);
    const QUuid accepted = host.requestDirectory();
    host.completeDirectory(accepted, QUrl::fromLocalFile("/tmp/music"));
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().at(0).toUuid(), accepted);
    QVERIFY(selected.first().at(1).toUrl().isLocalFile());
}

void PluginUiSettingsBridgeTest::invalidatedServicesIgnoreLateResults()
{
    Fixture f;
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request);
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    bridge.savePublicValues({{"server", "next"}});
    bridge.invalidate();
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), 0);
    HostPluginUiHostServices host;
    QSignalSpy selected(&host, &PluginUiHostServices::directorySelected);
    const auto id = host.requestDirectory();
    host.invalidate();
    host.completeDirectory(id, QUrl::fromLocalFile("/tmp/music"));
    QCOMPARE(selected.size(), 0);
}

void PluginUiSettingsBridgeTest::notificationDoesNotIncludeSecretData()
{
    HostPluginUiHostServices host;
    QSignalSpy notifications(&host, &PluginUiHostServices::notificationRequested);
    host.notify("PRIVATE-password", true);
    QCOMPARE(notifications.size(), 0);
    host.notify("plugin.ui.saved", false);
    QCOMPARE(notifications.size(), 1);
    QCOMPARE(notifications.first().first().toString(), QString("plugin.ui.saved"));
}

void PluginUiSettingsBridgeTest::createsRequiredPublicAndSecretFieldsAtomically()
{
    Fixture f;
    auto request = f.request;
    request.accountId = "new";
    for (auto &field : request.schema.first().fields) field.required = true;
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, request);
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    bridge.saveSettings({{"server", "new-server"}}, {{"password", "PRIVATE-new"}});
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished.first().at(1).toBool());
    QVERIFY(f.store.storedAccount("example", "new"));
    QVERIFY(!bridge.publicValues().contains("password"));
}

void PluginUiSettingsBridgeTest::exposesOnlyPublicVisibilityCondition()
{
    Fixture f;
    auto request = f.request;
    request.schema.first().fields.last().visibleWhen = SettingsVisibilityConditionV2{
        "server", SettingsComparisonV2::Equal, "changed"};
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, request);
    const auto fields = bridge.sections().first().toMap().value("fields").toList();
    const auto condition = fields.last().toMap().value("visibleWhen").toMap();
    QCOMPARE(condition.value("fieldId").toString(), QString("server"));
    QVERIFY(!fields.last().toMap().contains("value"));
}

void PluginUiSettingsBridgeTest::backendSecretsAreCppOnlyAndOmittedFromPresentation()
{
    Fixture f;
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request, QStringList{"password"});
    auto *service = qobject_cast<IPluginUiBackendSecretsV1 *>(&bridge); QVERIFY(service);
    QVERIFY(service->backendSecretConfigured("password"));
    QCOMPARE(*service->readBackendSecret("password"), QByteArray("PRIVATE-password"));
    QVERIFY(!bridge.secretConfigured("password"));
    const auto fields = bridge.sections().first().toMap().value("fields").toList();
    QCOMPARE(fields.size(), 1); QCOMPARE(fields.first().toMap().value("id").toString(), QString("server"));
    for (int i = 0; i < bridge.metaObject()->methodCount(); ++i) {
        const auto name = bridge.metaObject()->method(i).name();
        QVERIFY(!name.contains("BackendSecret")); QVERIFY(name != "backendSecretConfigured");
    }
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    bridge.saveSecret("password", "PRIVATE-QML");
    bridge.saveSettings({}, {{"password", "PRIVATE-QML"}});
    bridge.clearSecret("password");
    bridge.savePublicValues({{"password", "PRIVATE-QML"}});
    QTRY_COMPARE(finished.size(), 4);
    for (const auto &result : finished) QVERIFY(!result[1].toBool());
    QCOMPARE(*service->readBackendSecret("password"), QByteArray("PRIVATE-password"));
    const auto id = service->saveBackendSecret("password", "PRIVATE-业务-record");
    QTRY_COMPARE(finished.size(), 5); QCOMPARE(finished.last()[0].toUuid(), id); QVERIFY(finished.last()[1].toBool());
    QCOMPARE(*service->readBackendSecret("password"), QByteArray("PRIVATE-业务-record"));
    bridge.savePublicValues({{"server", "changed"}}); QTRY_COMPARE(finished.size(), 6); QVERIFY(finished.last()[1].toBool());
    QCOMPARE(*service->readBackendSecret("password"), QByteArray("PRIVATE-业务-record"));
    service->clearBackendSecret("password"); QTRY_COMPARE(finished.size(), 7); QVERIFY(finished.last()[1].toBool());
    QVERIFY(!service->backendSecretConfigured("password")); QVERIFY(!service->readBackendSecret("password"));
    QVERIFY(f.secrets.values.isEmpty());
}

void PluginUiSettingsBridgeTest::backendSecretDeclarationAndRecordsFailClosed()
{
    Fixture f;
    for (const auto &ids : {QStringList{"server"}, QStringList{"missing"}, QStringList{"password", "password"}}) {
        HostPluginUiSettingsBridge bad(&f.store, nullptr, f.request, ids);
        QVERIFY(bad.sections().isEmpty()); QVERIFY(bad.publicValues().isEmpty());
        QVERIFY(!bad.readBackendSecret("password"));
        QSignalSpy rejected(&bad, &PluginUiSettingsBridge::operationFinished);
        bad.saveBackendSecret("password", "PRIVATE-invalid"); QCoreApplication::processEvents();
        QCOMPARE(rejected.size(), 0);
        QCOMPARE(f.secrets.values.size(), 1);
    }
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request, QStringList{"password"});
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    for (const auto &bytes : {QByteArray(), QByteArray(65537, 'x'), QByteArray::fromHex("ff"), QByteArray("x\0y", 3)})
        bridge.saveBackendSecret("password", bytes);
    bridge.saveBackendSecret("server", "PRIVATE-invalid"); bridge.clearBackendSecret("server");
    QTRY_COMPARE(finished.size(), 6);
    for (const auto &result : finished) { QVERIFY(!result[1].toBool()); QVERIFY(!result[2].toString().contains("PRIVATE")); }
    QCOMPARE(*bridge.readBackendSecret("password"), QByteArray("PRIVATE-password"));
    HostPluginUiSettingsBridge ordinary(&f.store, nullptr, f.request);
    QVERIFY(!ordinary.readBackendSecret("password")); QVERIFY(ordinary.secretConfigured("password"));
    auto only = f.request; only.schema.first().fields.removeFirst();
    HostPluginUiSettingsBridge secretOnly(&f.store, nullptr, only, QStringList{"password"});
    QVERIFY(secretOnly.sections().isEmpty());
}

void PluginUiSettingsBridgeTest::backendSecretsRemainInstanceBoundAndInvalidatable()
{
    Fixture f;
    auto other = f.request; other.accountId = "other"; other.draft["password"] = "PRIVATE-other";
    QVERIFY(f.store.saveValidatedV2(other));
    HostPluginUiSettingsBridge bridge(&f.store, nullptr, f.request, QStringList{"password"});
    QCOMPARE(*bridge.readBackendSecret("password"), QByteArray("PRIVATE-password"));
    auto foreign = f.request; foreign.pluginPackageId = "org.example.foreign";
    HostPluginUiSettingsBridge conflict(&f.store, nullptr, foreign, QStringList{"password"});
    QVERIFY(!conflict.readBackendSecret("password")); QVERIFY(!conflict.backendSecretConfigured("password"));
    QSignalSpy rejected(&conflict, &PluginUiSettingsBridge::operationFinished);
    conflict.saveBackendSecret("password", "PRIVATE-hijack"); QTRY_COMPARE(rejected.size(), 1); QVERIFY(!rejected[0][1].toBool());
    bridge.invalidate(); QVERIFY(!bridge.readBackendSecret("password"));
    QSignalSpy finished(&bridge, &PluginUiSettingsBridge::operationFinished);
    bridge.saveBackendSecret("password", "PRIVATE-late"); bridge.clearBackendSecret("password");
    QCoreApplication::processEvents(); QCOMPARE(finished.size(), 0);
    QCOMPARE(f.store.resolvedAccountV2("example", "other")->secret,
             *encodeSourceSecretsV2({{"password", "PRIVATE-other"}}));
}

void PluginUiSettingsBridgeTest::backendNotificationMayDestroyService()
{
    Fixture f;
    auto *bridge = new HostPluginUiSettingsBridge(&f.store, nullptr, f.request, QStringList{"password"});
    QPointer<HostPluginUiSettingsBridge> guard(bridge);
    connect(bridge, &PluginUiSettingsBridge::changed, bridge, [bridge] { delete bridge; });
    const auto id = bridge->saveBackendSecret("password", "PRIVATE-reentrant");
    QVERIFY(!id.isNull()); QVERIFY(guard.isNull());
    QCOMPARE(f.store.resolvedAccountV2("example", "home")->secret,
             *encodeSourceSecretsV2({{"password", "PRIVATE-reentrant"}}));
    bridge = new HostPluginUiSettingsBridge(&f.store, nullptr, f.request, QStringList{"password"});
    guard = bridge;
    connect(bridge, &PluginUiSettingsBridge::changed, bridge, [bridge] { delete bridge; });
    const auto clearId = bridge->clearBackendSecret("password");
    QVERIFY(!clearId.isNull()); QVERIFY(guard.isNull()); QVERIFY(f.secrets.values.isEmpty());
    QCoreApplication::processEvents();
}

QTEST_MAIN(PluginUiSettingsBridgeTest)
#include "tst_PluginUiSettingsBridge.moc"
