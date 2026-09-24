#include "PluginManagementUiSession.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"

#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString validPackage = QStringLiteral("org.quemusic.source.ui-valid");
class Secrets final : public ISecretStore {
public:
    bool write(const QString &, const QByteArray &, QString *) override { return true; }
    std::optional<QByteArray> read(const QString &, QString *) const override { return {}; }
    bool remove(const QString &, QString *) override { return true; }
};
struct Fixture {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore store{&settings, &secrets};
    PluginManager manager;
    Fixture() { manager.addSearchPath(QUEMUSIC_UI_FIXTURE_ROOT); manager.discover(); }
    bool seed() {
        return store.saveValidatedV2({validPackage, "ui-valid", "home", "Home", true, 1, {}, {}});
    }
};
}

class PluginManagementUiSessionTest final : public QObject {
    Q_OBJECT
private slots:
    void createsEditContextFromHostIdentity();
    void rejectsUnsupportedModeAndInvalidComponentPath();
    void releasesLeaseAfterBackendIsDestroyed();
    void ignoresResultsAfterSessionInvalidation();
};

void PluginManagementUiSessionTest::createsEditContextFromHostIdentity()
{
    Fixture f;
    QVERIFY(f.seed());
    QVERIFY(f.manager.load(validPackage));
    PluginUiContextData data{validPackage, "ui-valid", "ui-valid/home", "home", PluginUiMode::Edit};
    PluginManagementUiSession session(&f.manager, &f.store, nullptr, data, {});
    QCOMPARE(session.state(), QString("ready"));
    QVERIFY(session.context());
    QCOMPARE(session.context()->accountId(), QString("home"));
    QCOMPARE(session.context()->sourceInstanceId(), QString("ui-valid/home"));
    QVERIFY(session.componentUrl().isLocalFile());
    QCOMPARE(f.manager.plugin(validPackage).activeLeases, 1);
}

void PluginManagementUiSessionTest::rejectsUnsupportedModeAndInvalidComponentPath()
{
    Fixture f;
    QVERIFY(f.manager.load(validPackage));
    PluginUiContextData bad{validPackage, "ui-valid", "ui-valid/foreign", "foreign", PluginUiMode::Edit};
    PluginManagementUiSession invalid(&f.manager, &f.store, nullptr, bad, {});
    QCOMPARE(invalid.state(), QString("error"));
    QVERIFY(!invalid.errorKey().isEmpty());
    QCOMPARE(f.manager.plugin(validPackage).activeLeases, 0);
    bad.mode = static_cast<PluginUiMode>(99);
    PluginManagementUiSession unsupported(&f.manager, &f.store, nullptr, bad, {});
    QCOMPARE(unsupported.state(), QString("error"));
    QCOMPARE(f.manager.plugin(validPackage).activeLeases, 0);
    const QString nullPackage = QStringLiteral("org.quemusic.source.ui-null_backend");
    QVERIFY(f.manager.load(nullPackage));
    PluginUiContextData missing{nullPackage, "ui-null_backend", {}, {}, PluginUiMode::Create};
    PluginManagementUiSession noBackend(&f.manager, &f.store, nullptr, missing, {});
    QCOMPARE(noBackend.state(), QString("error"));
    QCOMPARE(f.manager.plugin(nullPackage).activeLeases, 0);
}

void PluginManagementUiSessionTest::releasesLeaseAfterBackendIsDestroyed()
{
    Fixture f;
    QVERIFY(f.manager.load(validPackage));
    PluginUiContextData data{validPackage, "ui-valid", {}, {}, PluginUiMode::Create};
    PluginManagementUiSession session(&f.manager, &f.store, nullptr, data, {});
    QCOMPARE(session.state(), QString("ready"));
    QPointer<QObject> backend = session.context()->backend();
    QVERIFY(backend);
    bool destroyedBeforeLeaseRelease = false;
    QObject::connect(&f.manager, &PluginManager::pluginChanged, &session, [&] {
        if (f.manager.plugin(validPackage).activeLeases == 0)
            destroyedBeforeLeaseRelease = backend.isNull();
    });
    QCOMPARE(f.manager.unload(validPackage), PluginOperationResult::Busy);
    session.invalidateContext();
    QVERIFY(!session.context()->isValid());
    session.release();
    QVERIFY(backend.isNull());
    QVERIFY(destroyedBeforeLeaseRelease);
    session.release();
    QCOMPARE(f.manager.plugin(validPackage).activeLeases, 0);
    QCOMPARE(f.manager.unload(validPackage), PluginOperationResult::Success);
}

void PluginManagementUiSessionTest::ignoresResultsAfterSessionInvalidation()
{
    Fixture f;
    QVERIFY(f.manager.load(validPackage));
    PluginUiContextData data{validPackage, "ui-valid", {}, {}, PluginUiMode::Create};
    PluginManagementUiSession session(&f.manager, &f.store, nullptr, data, {});
    QCOMPARE(session.state(), QString("ready"));
    auto *host = session.context()->host();
    QVERIFY(host);
    session.invalidateContext();
    QVERIFY(session.context()->host() == nullptr);
    session.release();
    QCOMPARE(f.manager.plugin(validPackage).activeLeases, 0);
}

QTEST_MAIN(PluginManagementUiSessionTest)
#include "tst_PluginManagementUiSession.moc"
