#include "PluginSettingsOperation.h"
#include "fixtures/SettingsV2FixturePlugin.h"
#include <QSignalSpy>
#include <QTest>
#include <QEventLoop>
#include <QTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPluginLoader>
#include <QDir>
#include "PluginManifest.h"

namespace {
const QString packageId = "org.quemusic.source.settings-fixture";
int isolatedPinTest(const QString &test, QByteArray *output)
{
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QUEMUSIC_SETTINGS_PIN_CHILD", "1");
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {test});
    if (!child.waitForFinished(10000)) { child.kill(); child.waitForFinished(); return -1; }
    *output = child.readAll();
    return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
}
struct Harness {
    SettingsV2FixtureControl control;
    PluginManager manager;
    bool load() {
        manager.addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
        if (manager.discover() != 1 || !manager.load(packageId)) return false;
        manager.pluginInstance(packageId)->setProperty("control", QVariant::fromValue<void *>(&control));
        return true;
    }
    std::shared_ptr<PluginSettingsOperation> operation(QString action = {}, bool confirmed = false, int timeout = 200) {
        SettingsSchemaV2 schema{{"main", "main", {}, {{"diagnose", "diagnose", control.requiresConfirmation}}}};
        return std::make_shared<PluginSettingsOperation>(&manager,
            SourceConfigurationV2{packageId, "settings-fixture", "settings-fixture/draft", "draft", "Draft", {}, "PRIVATE"},
            schema, action, confirmed, timeout);
    }
};
}
class PluginSettingsOperationTest : public QObject {
    Q_OBJECT
private slots:
    void inlineProbeDeferredAndCleanup();
    void inlineFailureAndUnstarted_data();
    void inlineFailureAndUnstarted();
    void actionCorrelation_data();
    void actionCorrelation();
    void permission_data();
    void permission();
    void cancellationAndTimeout();
    void reentrantOwnerLoss_data();
    void reentrantOwnerLoss();
    void refusesForeignOwnership_data();
    void refusesForeignOwnership();
    void invalidIdentityClosesWithoutOpening();
    void externalDeletionDuringIdentityPinsBeforeOpen();
    void neverDispatchesAfterReadyRevoked_data();
    void neverDispatchesAfterReadyRevoked();
    void ignoresUnstartedAndForeignOpenFailure_data();
    void ignoresUnstartedAndForeignOpenFailure();
    void foreignDestructionNestedLoopCannotExposeUnloadability();
    void foreignDestructionNestedLoopCannotExposeUnloadability_data();
    void managerLossRetainsCallableLease();
    void ownershipViolationAfterManagerLoss_data();
    void ownershipViolationAfterManagerLoss();
    void managerLossDuringInvocation_data();
    void managerLossDuringInvocation();
    void asynchronousTerminalNestedLoopDefersCleanup();
};
void PluginSettingsOperationTest::inlineProbeDeferredAndCleanup()
{
    Harness h; QVERIFY(h.load()); auto op = h.operation(); QSignalSpy done(op.get(), &PluginSettingsOperation::finished);
    auto id = op->requestId(); QVERIFY(!id.isNull()); op->start(); QCOMPARE(done.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);
    QCOMPARE(done[0][0].toUuid(), id); auto result = done[0][1].toMap();
    QCOMPARE(result.value("success").toBool(), true); QCOMPARE(result.value("state").toInt(), int(SourceSessionStateV2::Ready));
    QVERIFY(!result.values().contains("PRIVATE")); QCOMPARE(h.control.closed, 1); QCOMPARE(h.control.destroyed, 1);
    QVERIFY(h.control.lastSession.isNull()); QCOMPARE(h.manager.plugin(packageId).activeLeases, 0);
    QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Success);
}
void PluginSettingsOperationTest::inlineFailureAndUnstarted_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("inline-failure") << int(SettingsV2FixtureControl::Failure);
    QTest::newRow("unstarted-return") << int(SettingsV2FixtureControl::Unstarted);
}
void PluginSettingsOperationTest::inlineFailureAndUnstarted()
{
    QFETCH(int, mode); Harness h; QVERIFY(h.load()); h.control.openMode = SettingsV2FixtureControl::Mode(mode);
    auto op = h.operation(); QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500); QVERIFY(!done[0][1].toMap().value("success").toBool());
    QVERIFY(!done[0][1].toMap().value("reasonKey").toString().contains("PRIVATE"));
    if (mode == SettingsV2FixtureControl::Unstarted) QCOMPARE(h.control.canceled, 0);
    QCOMPARE(h.control.invoked, 0); QCOMPARE(h.control.destroyed, 1);
}
void PluginSettingsOperationTest::actionCorrelation_data()
{
    QTest::addColumn<int>("mode"); QTest::addColumn<bool>("success");
    QTest::newRow("inline") << int(SettingsV2FixtureControl::Inline) << true;
    QTest::newRow("wrong-then-correct") << int(SettingsV2FixtureControl::WrongThenCorrect) << true;
    QTest::newRow("wrong-only") << int(SettingsV2FixtureControl::WrongOnly) << false;
    QTest::newRow("duplicate") << int(SettingsV2FixtureControl::Duplicate) << true;
    QTest::newRow("unstarted") << int(SettingsV2FixtureControl::Unstarted) << false;
    QTest::newRow("terminal-before-started") << int(SettingsV2FixtureControl::TerminalBeforeStarted) << false;
}
void PluginSettingsOperationTest::actionCorrelation()
{
    QFETCH(int, mode); QFETCH(bool, success); Harness h; QVERIFY(h.load());
    h.control.actionMode = SettingsV2FixtureControl::Mode(mode);
    auto op = h.operation("diagnose", false, 30); QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);
    auto r = done[0][1].toMap(); QCOMPARE(r.value("success").toBool(), success);
    QCOMPARE(r.value("actionId").toString(), QString("diagnose"));
    QTest::qWait(100); QCOMPARE(done.count(), 1); QCOMPARE(h.control.invoked, 1);
    QCOMPARE(h.control.destroyed, 1); QCOMPARE(h.manager.plugin(packageId).activeLeases, 0);
}
void PluginSettingsOperationTest::permission_data()
{
    QTest::addColumn<int>("scenario"); QTest::addColumn<int>("state");
    QTest::newRow("missing-account") << 0 << int(AvailabilityV2::Unavailable);
    QTest::newRow("denied-account") << 1 << int(AvailabilityV2::Forbidden);
    QTest::newRow("no-provider") << 2 << int(AvailabilityV2::Unsupported);
    QTest::newRow("constraints") << 3 << int(AvailabilityV2::Unsupported);
    QTest::newRow("confirmation") << 4 << int(AvailabilityV2::Unavailable);
    QTest::newRow("undeclared") << 5 << int(AvailabilityV2::Unsupported);
}
void PluginSettingsOperationTest::permission()
{
    QFETCH(int, scenario); QFETCH(int, state); Harness h; QVERIFY(h.load());
    if (scenario == 0) h.control.missingAccountGrant = true;
    if (scenario == 1) h.control.accountGrant = AvailabilityV2::Forbidden;
    if (scenario == 2) h.control.noActionProvider = true;
    if (scenario == 3) h.control.actionConstraints = {{"maxBitrate", 2}};
    if (scenario == 4) h.control.requiresConfirmation = true;
    auto op = h.operation(scenario == 5 ? "unknown" : "diagnose");
    QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);
    QCOMPARE(done[0][1].toMap().value("state").toInt(), state); QCOMPARE(h.control.invoked, 0);
    QCOMPARE(h.control.destroyed, 1);
}
void PluginSettingsOperationTest::cancellationAndTimeout()
{
    Harness h; QVERIFY(h.load()); h.control.openMode = SettingsV2FixtureControl::Delayed;
    auto op = h.operation({}, false, 10); QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500); QVERIFY(!done[0][1].toMap().value("success").toBool());
    QCOMPARE(h.control.canceled, 1); QCOMPARE(h.control.destroyed, 1);
    op = h.operation(); QSignalSpy canceled(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE(h.control.opened, 2); op->cancel(); QTest::qWait(120);
    QCOMPARE(h.control.canceled, 2); QCOMPARE(h.control.destroyed, 2);
    QCOMPARE(h.manager.plugin(packageId).activeLeases, 0);
}
void PluginSettingsOperationTest::reentrantOwnerLoss_data()
{
    QTest::addColumn<QString>("phase");
    QTest::newRow("create") << QString("create");
    QTest::newRow("open") << QString("open");
    QTest::newRow("action") << QString("action");
}
void PluginSettingsOperationTest::reentrantOwnerLoss()
{
    QFETCH(QString, phase); Harness h; QVERIFY(h.load()); auto op = h.operation("diagnose");
    bool observed = false, releasedAfterDestroy = false;
    QObject::connect(&h.manager, &PluginManager::pluginsChanged, &h.manager, [&] {
        if (observed && h.manager.plugin(packageId).activeLeases == 0)
            releasedAfterDestroy = h.control.closed == 1 && h.control.destroyed == 1;
    });
    h.control.callback = [&](const QString &event, QObject *) {
        if (event != phase || observed) return;
        observed = true; op->cancel(); op.reset();
        QCOMPARE(h.control.destroyed, 0);
        QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Busy);
    };
    op->start(); QTRY_VERIFY_WITH_TIMEOUT(observed, 500);
    QTRY_VERIFY_WITH_TIMEOUT(releasedAfterDestroy, 500);
    QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Success);
}
void PluginSettingsOperationTest::refusesForeignOwnership_data()
{
    QTest::addColumn<bool>("cancelInFactory");
    QTest::newRow("normal") << false;
    QTest::newRow("canceled-factory") << true;
}
void PluginSettingsOperationTest::refusesForeignOwnership()
{
    if (!qEnvironmentVariableIsSet("QUEMUSIC_SETTINGS_PIN_CHILD")) {
        QByteArray output;
        const auto status = isolatedPinTest(QString("refusesForeignOwnership:") + QTest::currentDataTag(), &output);
        QVERIFY2(status == 0, output.constData()); return;
    }
    QFETCH(bool, cancelInFactory);
    Harness h; QObject foreignOwner; QVERIFY(h.load()); h.control.forcedParent = &foreignOwner;
    auto op = h.operation(); QSignalSpy done(op.get(), &PluginSettingsOperation::finished);
    h.control.callback = [&](const QString &event, QObject *) { if (event == "create" && cancelInFactory) op->cancel(); };
    op->start(); QTRY_COMPARE(h.control.created, 1); QTest::qWait(30);
    QVERIFY(h.control.lastSession); QCOMPARE(h.control.lastSession->parent(), &foreignOwner);
    QCOMPARE(h.control.closed, 0); QCOMPARE(h.control.destroyed, 0);
    QVERIFY(!h.manager.plugin(packageId).busyReason.isEmpty());
    if (!cancelInFactory) { QCOMPARE(done.count(), 1); QVERIFY(!done[0][1].toMap().value("success").toBool()); }
    QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Busy);
    delete h.control.lastSession;
    QTRY_COMPARE(h.manager.plugin(packageId).activeLeases, 0);
    QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Busy);
}
void PluginSettingsOperationTest::invalidIdentityClosesWithoutOpening()
{
    Harness h; QVERIFY(h.load()); h.control.invalidIdentity = true;
    auto op = h.operation(); QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE(done.count(), 1); QVERIFY(!done[0][1].toMap().value("success").toBool());
    QCOMPARE(h.control.opened, 0); QCOMPARE(h.control.closed, 1); QCOMPARE(h.control.destroyed, 1);
    QCOMPARE(h.manager.plugin(packageId).activeLeases, 0);
}
void PluginSettingsOperationTest::externalDeletionDuringIdentityPinsBeforeOpen()
{
    if (!qEnvironmentVariableIsSet("QUEMUSIC_SETTINGS_PIN_CHILD")) {
        QByteArray output;
        const auto status = isolatedPinTest("externalDeletionDuringIdentityPinsBeforeOpen", &output);
        QVERIFY2(status == 0, output.constData());
        return;
    }
    // The safety loader keeps plugin code mapped throughout RED even if the
    // operation misses destruction and crashes on the next virtual call.
    QPluginLoader safetyLoader;
    Harness h;
    QVERIFY(h.load());
    const auto manifest = PluginManifest::fromFile(
        QDir(h.manager.plugin(packageId).path).filePath("manifest.json"));
    QVERIFY(manifest.isValid());
    safetyLoader.setFileName(manifest.libraryAbsolutePath());
    QVERIFY(safetyLoader.load());
    QCOMPARE(safetyLoader.instance(), h.manager.pluginInstance(packageId));
    h.control.callback = [&](const QString &event, QObject *session) {
        if (event == "identity") delete session;
    };
    auto op = h.operation();
    QSignalSpy done(op.get(), &PluginSettingsOperation::finished);
    op->start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);
    QCOMPARE(h.control.opened, 0);
    QCOMPARE(h.control.closed, 0);
    QCOMPARE(h.control.destroyed, 1);
    QCOMPARE(done[0][1].toMap().value("reasonKey").toString(),
             QString("source.settings.invalidOwnership"));
    QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Busy);
    safetyLoader.unload();
    QVERIFY(h.manager.pluginInstance(packageId));
}
void PluginSettingsOperationTest::neverDispatchesAfterReadyRevoked_data()
{
    QTest::addColumn<bool>("duringCapabilities");
    QTest::newRow("inline-ready-then-auth") << false;
    QTest::newRow("capability-callback-revokes-ready") << true;
}
void PluginSettingsOperationTest::neverDispatchesAfterReadyRevoked()
{
    QFETCH(bool, duringCapabilities); Harness h; QVERIFY(h.load());
    if (duringCapabilities) h.control.revokeReadyInCapabilities = true;
    else h.control.openMode = SettingsV2FixtureControl::ReadyThenAuth;
    auto op = h.operation("diagnose"); QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE(done.count(), 1); QVERIFY(!done[0][1].toMap().value("success").toBool());
    QCOMPARE(h.control.invoked, 0); QCOMPARE(h.control.closed, 1); QCOMPARE(h.control.destroyed, 1);
}
void PluginSettingsOperationTest::ignoresUnstartedAndForeignOpenFailure_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("before-started") << int(SettingsV2FixtureControl::FailureBeforeStarted);
    QTest::newRow("nested-foreign") << int(SettingsV2FixtureControl::NestedForeign);
}
void PluginSettingsOperationTest::ignoresUnstartedAndForeignOpenFailure()
{
    QFETCH(int, mode); Harness h; QVERIFY(h.load()); h.control.openMode = SettingsV2FixtureControl::Mode(mode);
    auto op = h.operation(); QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    QTRY_COMPARE(done.count(), 1); QVERIFY(done[0][1].toMap().value("success").toBool());
    QCOMPARE(h.control.canceled, 0); QCOMPARE(h.control.destroyed, 1);
}
void PluginSettingsOperationTest::foreignDestructionNestedLoopCannotExposeUnloadability_data()
{
    QTest::addColumn<bool>("foreignParent");
    QTest::newRow("foreign-parent") << true;
    QTest::newRow("external-delete-owned") << false;
}
void PluginSettingsOperationTest::foreignDestructionNestedLoopCannotExposeUnloadability()
{
    if (!qEnvironmentVariableIsSet("QUEMUSIC_SETTINGS_PIN_CHILD")) {
        QByteArray output;
        const auto status = isolatedPinTest(QString("foreignDestructionNestedLoopCannotExposeUnloadability:") + QTest::currentDataTag(), &output);
        QVERIFY2(status == 0, output.constData()); return;
    }
    QFETCH(bool, foreignParent);
    SettingsV2FixtureControl control; QObject foreignOwner;
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
    QCOMPARE(manager->discover(), 1); QVERIFY(manager->load(packageId));
    QPointer<QObject> root = manager->pluginInstance(packageId);
    root->setProperty("control", QVariant::fromValue<void *>(&control));
    if (foreignParent) control.forcedParent = &foreignOwner;
    else control.openMode = SettingsV2FixtureControl::Delayed;
    auto op = std::make_shared<PluginSettingsOperation>(manager.get(),
        SourceConfigurationV2{packageId, "settings-fixture", "settings-fixture/draft", "draft", "Draft", {}, {}},
        SettingsSchemaV2{});
    QSignalSpy done(op.get(), &PluginSettingsOperation::finished); op->start();
    if (foreignParent) { QTRY_COMPARE(done.count(), 1); }
    else { QTRY_COMPARE(control.opened, 1); }
    QVERIFY(control.lastSession);
    bool observed = false, exposedUnloadable = false;
    // Connected after the operation's destroyed listener. Never actually unload
    // in RED: the foreign plugin's deleting destructor is still on the stack.
    QObject::connect(control.lastSession, &QObject::destroyed, &foreignOwner, [&] {
        QEventLoop nested;
        QTimer::singleShot(20, &nested, [&] {
            const auto spec = manager->plugin(packageId);
            exposedUnloadable = spec.activeLeases == 0 && spec.busyReason.isEmpty();
            // Only destroy the manager when a retained lease or permanent pin
            // already prevents unloading; RED never attempts unsafe unload.
            if (!exposedUnloadable) manager.reset();
            observed = true;
            nested.quit();
        });
        nested.exec();
    });
    delete control.lastSession;
    QVERIFY(observed); QVERIFY(!exposedUnloadable);
    QVERIFY(!manager); QVERIFY(root);
    QCOMPARE(root->property("sourceSdkAbi").toInt(), 2);
}
void PluginSettingsOperationTest::managerLossRetainsCallableLease()
{
    // Safely characterize the dependency contract without an active plugin
    // method/session: losing the root here would be unsafe during open/run.
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
    QCOMPARE(manager->discover(), 1); QVERIFY(manager->load(packageId));
    QPointer<QObject> root = manager->pluginInstance(packageId);
    auto lease = manager->acquire(packageId); QVERIFY(lease.isValid());
    manager.reset();
    QVERIFY(lease.isValid());
    QVERIFY2(root, "PluginManager destruction unloaded the root while its callable lease survived");
}
void PluginSettingsOperationTest::ownershipViolationAfterManagerLoss_data()
{
    QTest::addColumn<bool>("foreignFactoryResult");
    QTest::newRow("manager-destroyed-inside-factory") << true;
    QTest::newRow("external-destruction-after-manager-loss") << false;
}
void PluginSettingsOperationTest::ownershipViolationAfterManagerLoss()
{
    if (!qEnvironmentVariableIsSet("QUEMUSIC_SETTINGS_PIN_CHILD")) {
        QByteArray output;
        const auto status = isolatedPinTest(QString("ownershipViolationAfterManagerLoss:") + QTest::currentDataTag(), &output);
        QVERIFY2(status == 0, output.constData()); return;
    }
    QFETCH(bool, foreignFactoryResult);
    SettingsV2FixtureControl control;
    // RED must never unmap code beneath a factory/destructor or live session.
    // This independent loader is only a test safety net, not the claimed pin:
    // assert replacement-manager quarantine, not root survival under this guard.
    QPluginLoader safetyLoader;
    QObject foreignOwner;
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
    QCOMPARE(manager->discover(), 1);
    const auto manifest = PluginManifest::fromFile(
        QDir(manager->plugin(packageId).path).filePath("manifest.json"));
    QVERIFY(manifest.isValid());
    safetyLoader.setFileName(manifest.libraryAbsolutePath());
    QVERIFY(safetyLoader.load());
    QVERIFY(manager->load(packageId));
    QPointer<QObject> root = manager->pluginInstance(packageId);
    QCOMPARE(safetyLoader.instance(), root.data());
    root->setProperty("control", QVariant::fromValue<void *>(&control));
    if (foreignFactoryResult) control.forcedParent = &foreignOwner;
    else control.openMode = SettingsV2FixtureControl::Delayed;
    bool managerLostInFactory = false;
    control.callback = [&](const QString &event, QObject *) {
        if (foreignFactoryResult && event == "create") {
            manager.reset();
            managerLostInFactory = true;
        }
    };
    auto op = std::make_shared<PluginSettingsOperation>(manager.get(),
        SourceConfigurationV2{packageId, "settings-fixture", "settings-fixture/draft", "draft", "Draft", {}, {}},
        SettingsSchemaV2{});
    op->start();
    if (foreignFactoryResult) {
        QTRY_VERIFY(managerLostInFactory);
        QVERIFY(control.lastSession);
        QCOMPARE(control.lastSession->parent(), &foreignOwner);
        QCOMPARE(control.closed, 0);
        QCOMPARE(control.destroyed, 0);
    } else {
        QTRY_COMPARE(control.opened, 1);
        manager.reset();
        // Before the canceled operation's queued cleanup: the unexpected
        // destroyed observer is still installed, but its manager is now null.
        delete control.lastSession;
        QCOMPARE(control.destroyed, 1);
        QCOMPARE(control.closed, 0);
    }
    QVERIFY(!manager);
    QTest::qWait(30);
    op.reset();
    PluginManager replacement;
    replacement.addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
    QCOMPARE(replacement.discover(), 1);
    const auto reason = replacement.plugin(packageId).busyReason;
    const bool accepted = replacement.load(packageId);
    // Foreign owner/test alone deletes its session, while safetyLoader keeps
    // the fixture callable even in RED. The operation must never do so.
    if (foreignFactoryResult) {
        QCOMPARE(control.closed, 0);
        QCOMPARE(control.destroyed, 0);
        delete control.lastSession;
    }
    QVERIFY2(!accepted, "R7 quarantine was skipped after manager loss: replacement manager loaded the offending package");
    QCOMPARE(reason, QString("source.external-destruction.restart-required"));
    // Drop the test safety reference only after proven quarantine; in GREEN
    // the production pin alone must keep the root callable after all facades.
    safetyLoader.unload();
    QVERIFY(root);
    QCOMPARE(root->property("sourceSdkAbi").toInt(), 2);
}
void PluginSettingsOperationTest::managerLossDuringInvocation_data()
{
    QTest::addColumn<QString>("phase");
    for (auto phase : {"create", "open", "action"}) QTest::newRow(phase) << QString(phase);
}
void PluginSettingsOperationTest::managerLossDuringInvocation()
{
    QFETCH(QString, phase);
    SettingsV2FixtureControl control;
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
    QCOMPARE(manager->discover(), 1); QVERIFY(manager->load(packageId));
    QPointer<QObject> root = manager->pluginInstance(packageId);
    root->setProperty("control", QVariant::fromValue<void *>(&control));
    bool observed = false, retained = false;
    control.callback = [&](const QString &event, QObject *) {
        if (event != phase || observed) return;
        observed = true;
        manager.reset();
        retained = root && control.destroyed == 0;
    };
    auto op = std::make_shared<PluginSettingsOperation>(manager.get(),
        SourceConfigurationV2{packageId, "settings-fixture", "settings-fixture/draft", "draft", "Draft", {}, {}},
        SettingsSchemaV2{{"main", "main", {}, {{"diagnose", "diagnose", false}}}}, "diagnose");
    QSignalSpy done(op.get(), &PluginSettingsOperation::finished);
    op->start();
    QTRY_VERIFY(observed); QVERIFY(retained);
    QTRY_COMPARE(control.destroyed, 1);
    QCOMPARE(control.closed, 1); QVERIFY(root.isNull()); QCOMPARE(done.count(), 0);
    PluginManager replacement;
    replacement.addSearchPath(QUEMUSIC_SETTINGS_PACKAGES);
    QCOMPARE(replacement.discover(), 1); QVERIFY(replacement.load(packageId));
    QCOMPARE(replacement.unload(packageId), PluginOperationResult::Success);
}
void PluginSettingsOperationTest::asynchronousTerminalNestedLoopDefersCleanup()
{
    // Test-only loader prevents unmapped code in RED, when the operation may
    // destroy the session from a nested loop inside its asynchronous signal.
    QPluginLoader safetyLoader;
    Harness h; QVERIFY(h.load());
    const auto manifest = PluginManifest::fromFile(
        QDir(h.manager.plugin(packageId).path).filePath("manifest.json"));
    QVERIFY(manifest.isValid());
    safetyLoader.setFileName(manifest.libraryAbsolutePath()); QVERIFY(safetyLoader.load());
    h.control.openMode = SettingsV2FixtureControl::Delayed;
    QObject observer;
    bool observed = false, destroyedInside = false;
    h.control.callback = [&](const QString &event, QObject *session) {
        if (event != "open") return;
        // Installed after the operation's listener. A timer posted by that
        // listener can run here before the provider's signal emitter returns.
        connect(static_cast<IMusicSourceSessionV2 *>(session), &IMusicSourceSessionV2::stateChanged,
            &observer, [&](SourceSessionStateV2 state) {
                if (state != SourceSessionStateV2::Ready) return;
                QEventLoop nested;
                QTimer::singleShot(20, &nested, [&] {
                    destroyedInside = h.control.destroyed != 0;
                    observed = true; nested.quit();
                });
                nested.exec();
            });
    };
    auto op = h.operation(); QSignalSpy done(op.get(), &PluginSettingsOperation::finished);
    op->start(); QTRY_VERIFY(observed);
    QVERIFY2(!destroyedInside, "Cleanup ran inside the still-emitting asynchronous provider stack");
    QTRY_COMPARE(done.count(), 1); QCOMPARE(h.control.destroyed, 1); QCOMPARE(h.control.closed, 1);
    safetyLoader.unload();
    QCOMPARE(h.manager.unload(packageId), PluginOperationResult::Success);
}
QTEST_GUILESS_MAIN(PluginSettingsOperationTest)
#include "tst_PluginSettingsOperation.moc"
