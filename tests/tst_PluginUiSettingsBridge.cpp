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

QTEST_MAIN(PluginUiSettingsBridgeTest)
#include "tst_PluginUiSettingsBridge.moc"
