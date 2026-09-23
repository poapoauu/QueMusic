#include "PluginManager.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"

#include <QScopedPointer>
#include <QTest>

class PluginUiUnloadTest final : public QObject {
    Q_OBJECT
private slots:
    void leaseProtectsBackendUntilTeardown();
    void rejectedProviderRetainsNoLease();
};

void PluginUiUnloadTest::leaseProtectsBackendUntilTeardown()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT));
    QCOMPARE(manager.discover(), 7);
    const QString id = QStringLiteral("org.quemusic.source.ui-valid");
    QVERIFY(manager.load(id));
    PluginLease lease = manager.acquire(id);
    QVERIFY(lease.isValid());
    auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager.pluginInstance(id));
    QVERIFY(provider);
    QScopedPointer<QObject> owner(new QObject);
    PluginUiContextData context{id, QStringLiteral("ui-valid"),
                                QStringLiteral("ui-valid/home"), QStringLiteral("home"),
                                PluginUiMode::Edit};
    QObject *backend = provider->createManagementBackend(context, owner.data());
    QVERIFY(backend);
    QCOMPARE(backend->parent(), owner.data());
    QCOMPARE(manager.unload(id), PluginOperationResult::Busy);
    owner.reset();
    lease = {};
    QCOMPARE(manager.unload(id), PluginOperationResult::Success);
}

void PluginUiUnloadTest::rejectedProviderRetainsNoLease()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_UI_FIXTURE_ROOT));
    QCOMPARE(manager.discover(), 7);
    const QString id = QStringLiteral("org.quemusic.source.ui-provider_missing");
    QVERIFY(!manager.load(id));
    QCOMPARE(manager.plugin(id).activeLeases, 0);
    QVERIFY(!manager.acquire(id).isValid());
    QCOMPARE(manager.unload(id), PluginOperationResult::Success);
}

QTEST_MAIN(PluginUiUnloadTest)
#include "tst_PluginUiUnload.moc"
