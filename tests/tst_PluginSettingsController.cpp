#include "PluginSettingsController.h"
#include "SourceRegistry.h"
#include "SourceAccountStore.h"
#include "fixtures/SettingsV2FixturePlugin.h"
#include "PluginManagementUiSession.h"
#include <QSettings>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString packageId = "org.quemusic.source.settings-fixture";
class Secrets final : public ISecretStore {
public:
    QMap<QString, QByteArray> values;
    int writes = 0, removes = 0; mutable int reads = 0;
    bool failWrite = false;
    bool write(const QString &id, const QByteArray &value, QString *) override {
        ++writes; if (failWrite) return false; values[id] = value; return true;
    }
    bool remove(const QString &id, QString *) override { ++removes; values.remove(id); return true; }
    std::optional<QByteArray> read(const QString &id, QString *) const override {
        ++reads; if (!values.contains(id)) return {}; return values.value(id);
    }
};
struct Harness {
    QTemporaryDir temp;
    QSettings settings{temp.filePath("settings.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore store{&settings, &secrets};
    SettingsV2FixtureControl control;
    PluginManager manager;
    SourceRegistry registry{&manager, &store};
    std::unique_ptr<PluginSettingsController> controller;
    bool load() {
        manager.addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
        if (manager.discover() != 1 || !manager.load(packageId)) return false;
        manager.pluginInstance(packageId)->setProperty("control", QVariant::fromValue<void *>(&control));
        controller = std::make_unique<PluginSettingsController>(&manager, &registry, &store);
        return true;
    }
};
struct UiHarness {
    QTemporaryDir temp;
    QSettings settings{temp.filePath("ui.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore store{&settings, &secrets};
    PluginManager manager;
    SourceRegistry registry{&manager, &store};
    std::unique_ptr<PluginSettingsController> controller;
    const QString id = QStringLiteral("org.quemusic.source.ui-valid");
    bool load() {
        manager.addSearchPath(QUEMUSIC_UI_FIXTURE_ROOT);
        if (manager.discover() != 7 || !manager.load(id)) return false;
        controller = std::make_unique<PluginSettingsController>(&manager, &registry, &store);
        return controller->selectPlugin(id);
    }
};
QVariantMap field(const PluginSettingsController &c, const QString &id) {
    for (const auto &s : c.settingsSections()) for (const auto &v : s.toMap().value("fields").toList())
        if (v.toMap().value("id").toString() == id) return v.toMap();
    return {};
}
QVariantMap capability(const PluginSettingsController &c, SourceActionV2 action) {
    for (const auto &v : c.sourceCapabilities()) if (v.toMap().value("action").toInt() == int(action)) return v.toMap();
    return {};
}
bool privateData(const QVariant &v) {
    if (v.metaType().id() == QMetaType::QVariantMap) {
        auto map = v.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (QStringList{"secretReference", "credentialReference", "secretFormat", "defaultValue", "secretUpdates", "parameters", "path", "error", "busyReason"}.contains(it.key())) return true;
            if (privateData(it.value())) return true;
        }
    } else if (v.metaType().id() == QMetaType::QVariantList) {
        for (const auto &item : v.toList()) if (privateData(item)) return true;
    } else if (v.toString().contains("PRIVATE")) return true;
    return false;
}
}

class PluginSettingsControllerTest : public QObject {
    Q_OBJECT
private slots:
    void genericCrudAndForeignIdentity();
    void safeSnapshotsAndLeaseStable();
    void directoryAdapter();
    void partialDraftUsesSelectedVisibilityContext();
    void draftProbeNeverPersists();
    void failurePreservesRunningSession();
    void probePermissionsInvalidateAndRecheck();
    void musicCapabilityDiagnostics();
    void musicCapabilityConstraintTypes_data();
    void musicCapabilityConstraintTypes();
    void invalidSchemaAndNoSchema();
    void deletionDuringInvocation_data();
    void deletionDuringInvocation();
    void selectionAndEditsCancel();
    void managementUiCreateEditAndClose();
    void deferredUnloadWaitsForPage();
    void selectionWaitsForPageTeardown();
    void activeInstanceMutationWaitsForPage();
    void managementUiFallbackAndReload();
};
void PluginSettingsControllerTest::managementUiCreateEditAndClose()
{
    UiHarness h; QVERIFY(h.load()); auto &c = *h.controller;
    QVERIFY(c.managementUiAvailable());
    QVERIFY(c.openManagementUi(PluginUiMode::Create));
    QVERIFY(c.managementUiSession());
    QCOMPARE(c.managementUiSession()->context()->mode(), PluginUiMode::Create);
    QVERIFY(!c.managementUiSession()->context()->accountId().isEmpty());
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 1);
    c.beginCloseManagementUi();
    QVERIFY(!c.managementUiSession()->context()->isValid());
    c.finishCloseManagementUi();
    QVERIFY(!c.managementUiSession());
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 0);
    QVERIFY(h.store.saveValidatedV2({h.id, "ui-valid", "home", "Home", true, 1, {}, {}}));
    QVERIFY(c.selectInstance("ui-valid/home"));
    QVERIFY(c.openManagementUi(PluginUiMode::Edit));
    QCOMPARE(c.managementUiSession()->context()->sourceInstanceId(), QString("ui-valid/home"));
    c.beginCloseManagementUi(); c.finishCloseManagementUi();
}
void PluginSettingsControllerTest::deferredUnloadWaitsForPage()
{
    UiHarness h; QVERIFY(h.load()); auto &c = *h.controller;
    QVERIFY(c.openManagementUi(PluginUiMode::Create));
    QSignalSpy closeRequested(&c, &PluginSettingsController::managementUiCloseRequested);
    QVERIFY(c.unloadPlugin(h.id));
    QCOMPARE(closeRequested.size(), 1);
    QCOMPARE(h.manager.plugin(h.id).state, PluginState::Loaded);
    c.beginCloseManagementUi(); c.finishCloseManagementUi();
    QCOMPARE(h.manager.plugin(h.id).state, PluginState::Unloaded);
    QVERIFY(!c.managementUiSession());
}
void PluginSettingsControllerTest::selectionWaitsForPageTeardown()
{
    UiHarness h; QVERIFY(h.load()); auto &c = *h.controller;
    QVERIFY(c.openManagementUi(PluginUiMode::Create));
    QSignalSpy closeRequested(&c, &PluginSettingsController::managementUiCloseRequested);
    QVERIFY(c.selectInstance(""));
    QCOMPARE(closeRequested.size(), 1);
    QVERIFY(c.managementUiSession());
    c.beginCloseManagementUi(); c.finishCloseManagementUi();
    QVERIFY(!c.managementUiSession());
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 0);
}
void PluginSettingsControllerTest::activeInstanceMutationWaitsForPage()
{
    UiHarness h; QVERIFY(h.load()); auto &c = *h.controller;
    QVERIFY(h.store.saveValidatedV2({h.id, "ui-valid", "home", "Home", true, 1, {}, {}}));
    QVERIFY(c.selectInstance("ui-valid/home"));
    QVERIFY(c.openManagementUi(PluginUiMode::Edit));
    QSignalSpy closeRequested(&c, &PluginSettingsController::managementUiCloseRequested);
    QVERIFY(c.setInstanceEnabled("ui-valid/home", false));
    QCOMPARE(closeRequested.size(), 1);
    QVERIFY(h.store.storedAccount("ui-valid", "home")->enabled);
    c.beginCloseManagementUi(); c.finishCloseManagementUi();
    QVERIFY(!h.store.storedAccount("ui-valid", "home")->enabled);
    QVERIFY(c.selectInstance("ui-valid/home"));
    QVERIFY(!c.openManagementUi(PluginUiMode::Edit));
    QVERIFY(h.store.setEnabled("ui-valid", "home", true));
    QVERIFY(c.openManagementUi(PluginUiMode::Edit));
    QVERIFY(c.removeInstance("ui-valid/home"));
    QVERIFY(h.store.storedAccount("ui-valid", "home"));
    c.beginCloseManagementUi(); c.finishCloseManagementUi();
    QVERIFY(!h.store.storedAccount("ui-valid", "home"));
}
void PluginSettingsControllerTest::managementUiFallbackAndReload()
{
    Harness schema; QVERIFY(schema.load());
    QVERIFY(schema.controller->selectPlugin(packageId));
    QVERIFY(!schema.controller->managementUiAvailable());
    QVERIFY(!schema.controller->openManagementUi(PluginUiMode::Create));
    QVERIFY(!schema.controller->managementUiSession());

    UiHarness h; QVERIFY(h.load()); auto &c = *h.controller;
    QVERIFY(c.openManagementUi(PluginUiMode::Create));
    QVERIFY(c.reloadPlugin(h.id));
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 1);
    c.beginCloseManagementUi(); c.finishCloseManagementUi();
    QCOMPARE(h.manager.plugin(h.id).state, PluginState::Loaded);
    QCOMPARE(h.manager.plugin(h.id).activeLeases, 0);
    QVERIFY(!c.managementUiSession());
}
void PluginSettingsControllerTest::genericCrudAndForeignIdentity()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    QSignalSpy observer(&h.registry, &SourceRegistry::instanceChanged), reset(&c, &PluginSettingsController::draftReset);
    QVERIFY(c.setDraftValues({{"folder", "/one"}})); QVERIFY(c.saveInstance("One", {{"password", "PRIVATE-one"}}));
    const auto one = c.selectedInstanceId(); QVERIFY(!one.isEmpty());
    QCOMPARE(observer.count(), 1); QVERIFY(!c.instances().isEmpty()); QVERIFY(reset.count() > 0);
    QVERIFY(c.selectInstance("")); QVERIFY(c.saveInstance("Two", {})); const auto two = c.selectedInstanceId(); QVERIFY(two != one);
    QCOMPARE(h.store.accounts().size(), 2); QCOMPARE(c.instances().size(), 2);
    QVERIFY(c.setInstanceEnabled(one, false)); QVERIFY(c.selectInstance(one));
    QCOMPARE(c.instances().first().toMap().contains("enabled"), true);
    QVERIFY(c.saveInstance("One edited", {{"password", ""}}));
    const auto account = h.store.storedAccount("settings-fixture", one.section('/', 1));
    QVERIFY(account); QVERIFY(!account->enabled); QCOMPARE(account->parameters.value("folder").toString(), QString("/one"));
    QVERIFY(!c.selectInstance("settings-fixture/unknown")); QCOMPARE(c.selectedInstanceId(), one);
    QVERIFY(h.store.saveValidatedV2({"org.example.foreign", "settings-fixture", "foreign", "Foreign", true, 1, {}, {}}));
    QVERIFY(!c.selectInstance("settings-fixture/foreign")); QVERIFY(!c.removeInstance("settings-fixture/foreign"));
    QVERIFY(c.removeInstance(two)); QCOMPARE(c.instances().size(), 1); QVERIFY(c.removeInstance(one));
    QVERIFY(c.selectedInstanceId().isEmpty()); QVERIFY(c.instances().isEmpty()); QCOMPARE(h.control.created, 0);
}
void PluginSettingsControllerTest::safeSnapshotsAndLeaseStable()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    QSignalSpy changes(&c, &PluginSettingsController::snapshotsChanged);
    QVERIFY(!c.setDraftValues({{"password", "PRIVATE-public"}})); QVERIFY(!c.setDraftValues({{"folder", QVariantMap{{"token", "PRIVATE"}}}}));
    QVERIFY(c.saveInstance("Account", {{"password", "PRIVATE-stored"}}));
    QVERIFY(field(c, "password").value("credentialConfigured").toBool()); QVERIFY(!field(c, "password").contains("value"));
    for (const auto &value : {QVariant(c.plugins()), QVariant(c.instances()), QVariant(c.settingsSections()),
                             QVariant(c.settingsActions()), QVariant(c.sourceCapabilities()), QVariant(c.selectedPlugin())}) QVERIFY(!privateData(value));
    QCoreApplication::processEvents(); const auto count = changes.count(); QTest::qWait(80);
    QCOMPARE(changes.count(), count); QCOMPARE(h.manager.plugin(packageId).activeLeases, 0);
    QVERIFY(c.unloadPlugin(packageId)); QVERIFY(c.settingsSections().isEmpty()); QVERIFY(c.selectedInstanceId().isEmpty());
    QCOMPARE(h.control.created, 0);
}
void PluginSettingsControllerTest::directoryAdapter()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    QVERIFY(c.setDirectoryField("folder", QUrl("file:///tmp/a%20b/100%25")));
    QCOMPARE(field(c, "folder").value("value").toString(), QString("/tmp/a b/100%"));
    QVERIFY(!c.setDirectoryField("folder", QUrl("https://example.invalid/path")));
    QVERIFY(!c.setDirectoryField("password", QUrl("file:///tmp/x")));
    QVERIFY(!c.setDirectoryField("active", QUrl("file:///tmp/x")));
    QVERIFY(!c.setDirectoryField("folder", QUrl()));
    QCOMPARE(field(c, "folder").value("value").toString(), QString("/tmp/a b/100%"));
}
void PluginSettingsControllerTest::partialDraftUsesSelectedVisibilityContext()
{
    Harness h;
    h.control.conditionalRequiredFolder = true;
    QVERIFY(h.load());
    auto &c = *h.controller;
    QVERIFY(c.selectPlugin(packageId));
    // A new draft has known empty previous context, so the true default still
    // makes an explicitly empty folder invalid.
    QVERIFY(!c.setDraftValues({{"folder", ""}}));
    QCOMPARE(c.lastErrorKey(), QString("source.settings.invalidValue"));
    QVERIFY(c.setDraftValues({{"active", false}, {"folder", "/stored"}}));
    QVERIFY(c.saveInstance("Hidden folder", {}));

    // The replacement draft omits the condition field. Its selected stored
    // value (false), not the schema default (true), keeps folder non-required.
    QVERIFY(c.setDraftValues({{"folder", ""}}));
    QCOMPARE(field(c, "active").value("value").toBool(), false);
    QCOMPARE(field(c, "folder").value("visible").toBool(), false);
    QCOMPARE(field(c, "folder").value("value").toString(), QString());
    QVERIFY(c.saveInstance("Cleared hidden folder", {}));
    const auto stored = h.store.storedAccount("settings-fixture", c.selectedInstanceId().section('/', 1));
    QVERIFY(stored);
    QCOMPARE(stored->parameters.value("active").toBool(), false);
    QCOMPARE(stored->parameters.value("folder").toString(), QString());
}
void PluginSettingsControllerTest::draftProbeNeverPersists()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    QVERIFY(c.setDraftValues({{"folder", "/draft"}})); const auto keys = h.settings.allKeys();
    QSignalSpy done(&c, &PluginSettingsController::connectionTestFinished);
    const auto id = c.testConnection({{"password", "PRIVATE-draft"}}); QVERIFY(!id.isNull()); QCOMPARE(done.count(), 0);
    QTRY_COMPARE(done.count(), 1); QVERIFY(done[0][1].toMap().value("success").toBool());
    QCOMPARE(h.settings.allKeys(), keys); QCOMPARE(h.secrets.writes, 0); QCOMPARE(h.secrets.removes, 0);
    QCOMPARE(h.control.created, 1); QCOMPARE(h.control.destroyed, 1); QCOMPARE(h.registry.children().size(), 0);
    const auto draftId = h.control.lastConfiguration.accountId;
    QVERIFY(c.saveInstance("Saved", {})); QCOMPARE(c.selectedInstanceId(), QString("settings-fixture/") + draftId);
    QCOMPARE(h.manager.plugin(packageId).activeLeases, 0);
}
void PluginSettingsControllerTest::failurePreservesRunningSession()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    QVERIFY(c.saveInstance("Saved", {{"password", "PRIVATE-old"}}));
    auto id = c.selectedInstanceId(); QPointer<IMusicSourceSessionV2> live = h.registry.sessionFor(id); QVERIFY(live);
    QVERIFY(c.setDraftValues({{"folder", "/pending"}})); h.secrets.failWrite = true;
    QVERIFY(!c.saveInstance("Failed", {{"password", "PRIVATE-new"}})); QVERIFY(live);
    QCOMPARE(field(c, "folder").value("value").toString(), QString("/pending"));
    QVERIFY(!c.unloadPlugin(packageId)); QVERIFY(c.lastErrorKey().startsWith("source.settings.")); QVERIFY(live);
    h.secrets.failWrite = false; QVERIFY(c.saveInstance("Success", {})); QVERIFY(live.isNull());
}
void PluginSettingsControllerTest::probePermissionsInvalidateAndRecheck()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    QCOMPARE(c.settingsActions().first().toMap().value("state").toInt(), int(AvailabilityV2::Unavailable));
    QSignalSpy probe(&c, &PluginSettingsController::connectionTestFinished), action(&c, &PluginSettingsController::settingsActionFinished);
    c.testConnection({}); QTRY_COMPARE(probe.count(), 1);
    QCOMPARE(c.settingsActions().first().toMap().value("state").toInt(), int(AvailabilityV2::Available));
    h.control.accountGrant = AvailabilityV2::Forbidden;
    QVERIFY(!c.runSettingsAction("diagnose", {}).isNull()); QTRY_COMPARE(action.count(), 1);
    QCOMPARE(action[0][1].toMap().value("state").toInt(), int(AvailabilityV2::Forbidden)); QCOMPARE(h.control.invoked, 0);
    QVERIFY(c.setDraftValues({{"folder", "/changed"}}));
    QCOMPARE(c.settingsActions().first().toMap().value("state").toInt(), int(AvailabilityV2::Unavailable));
}
void PluginSettingsControllerTest::musicCapabilityDiagnostics()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    auto play = capability(c, SourceActionV2::Play); QVERIFY(!play.isEmpty());
    QCOMPARE(play.value("pluginState").toInt(), int(AvailabilityV2::Available));
    QCOMPARE(play.value("serverState").toInt(), int(AvailabilityV2::Unavailable));
    QCOMPARE(play.value("accountState").toInt(), int(AvailabilityV2::Unavailable)); QCOMPARE(h.control.created, 0);
    QSignalSpy done(&c, &PluginSettingsController::connectionTestFinished); c.testConnection({}); QTRY_COMPARE(done.count(), 1);
    play = capability(c, SourceActionV2::Play); QCOMPARE(play.value("state").toInt(), int(AvailabilityV2::Forbidden));
    QCOMPARE(capability(c, SourceActionV2::Favorite).value("state").toInt(), int(AvailabilityV2::Unavailable));
    QCOMPARE(capability(c, SourceActionV2::Lyrics).value("state").toInt(), int(AvailabilityV2::Unsupported));
    QVERIFY(!privateData(c.sourceCapabilities())); QVERIFY(c.setDraftValues({{"active", false}}));
    QCOMPARE(capability(c, SourceActionV2::Play).value("accountState").toInt(), int(AvailabilityV2::Unavailable));
    QCOMPARE(h.control.created, 1);
}
void PluginSettingsControllerTest::musicCapabilityConstraintTypes_data()
{
    QTest::addColumn<QVariant>("maximum"); QTest::addColumn<int>("expected");
    QTest::newRow("supported-integer") << QVariant(320) << int(AvailabilityV2::Available);
    QTest::newRow("supported-double") << QVariant(320.5) << int(AvailabilityV2::Available);
    QTest::newRow("unsupported-short") << QVariant::fromValue(short(320)) << int(AvailabilityV2::Unavailable);
    QTest::newRow("unsupported-string") << QVariant("320") << int(AvailabilityV2::Unavailable);
}
void PluginSettingsControllerTest::musicCapabilityConstraintTypes()
{
    QFETCH(QVariant, maximum); QFETCH(int, expected);
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    h.control.musicCapabilities.serverActions[SourceActionV2::Play] =
        {AvailabilityV2::Available, "PRIVATE", {{"maxBitrate", maximum}}};
    h.control.musicCapabilities.accountActions[SourceActionV2::Play] = {AvailabilityV2::Available, {}, {}};
    QSignalSpy done(&c, &PluginSettingsController::connectionTestFinished);
    QVERIFY(!c.testConnection({}).isNull()); QTRY_COMPARE(done.count(), 1);
    QCOMPARE(capability(c, SourceActionV2::Play).value("state").toInt(), expected);
}
void PluginSettingsControllerTest::invalidSchemaAndNoSchema()
{
    Harness h; h.control.invalidSchema = true; QVERIFY(h.load()); auto &c = *h.controller;
    QVERIFY(c.selectPlugin(packageId)); QVERIFY(!c.selectedPlugin().value("settingsAvailable").toBool());
    QVERIFY(!c.saveInstance("invalid", {})); QVERIFY(c.testConnection({}).isNull());
    QDir packageRoot(QUEMUSIC_NO_SETTINGS_PACKAGE); QVERIFY(packageRoot.cdUp());
    h.manager.addSearchPath(packageRoot.absolutePath()); c.discoverPlugins();
    const QString other = "org.quemusic.source.fixture-v2";
    QVERIFY(c.loadPlugin(other)); QVERIFY(c.selectPlugin(other));
    QVERIFY(!c.selectedPlugin().value("settingsAvailable").toBool()); QVERIFY(!c.saveInstance("no-schema", {}));
    QVERIFY(c.unloadPlugin(other));
}
void PluginSettingsControllerTest::deletionDuringInvocation_data()
{
    QTest::addColumn<QString>("phase");
    for (auto phase : {"create", "open", "action"}) QTest::newRow(phase) << QString(phase);
}
void PluginSettingsControllerTest::deletionDuringInvocation()
{
    QFETCH(QString, phase); Harness h; QVERIFY(h.load()); QVERIFY(h.controller->selectPlugin(packageId));
    bool seen = false;
    h.control.callback = [&](const QString &event, QObject *) {
        if (event != phase || seen) return; seen = true; h.controller.reset();
        QCOMPARE(h.control.destroyed, 0); QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Busy);
    };
    auto id = h.controller->runSettingsAction("diagnose", {}); QVERIFY(!id.isNull());
    QTRY_VERIFY(seen); QTRY_COMPARE(h.control.destroyed, 1); QVERIFY(h.control.lastSession.isNull());
    QCOMPARE(h.manager.plugin(packageId).activeLeases, 0); QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Success);
}
void PluginSettingsControllerTest::selectionAndEditsCancel()
{
    Harness h; QVERIFY(h.load()); auto &c = *h.controller; QVERIFY(c.selectPlugin(packageId));
    h.control.openMode = SettingsV2FixtureControl::Delayed;
    QSignalSpy done(&c, &PluginSettingsController::connectionTestFinished);
    QVERIFY(!c.testConnection({}).isNull()); QTRY_COMPARE(h.control.opened, 1);
    QVERIFY(c.setDraftValues({{"folder", "/cancel"}})); QTest::qWait(120); QCOMPARE(done.count(), 0); QVERIFY(!c.busy());
    QVERIFY(!c.testConnection({}).isNull()); QTRY_COMPARE(h.control.opened, 2);
    QVERIFY(c.selectInstance("")); QTest::qWait(120); QCOMPARE(done.count(), 0); QCOMPARE(h.control.destroyed, 2);
}
QTEST_GUILESS_MAIN(PluginSettingsControllerTest)
#include "tst_PluginSettingsController.moc"
