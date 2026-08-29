#include "PluginManager.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

class PluginManagerTest : public QObject {
    Q_OBJECT

private slots:
    void refusesUnloadWhileLeaseIsActive();
    void reloadsAfterFinalLeaseIsReleased();
    void malformedPackageDoesNotBlockValidPackage();
};

void PluginManagerTest::refusesUnloadWhileLeaseIsActive()
{
    PluginManager manager;
    QSignalSpy failures(&manager, &PluginManager::pluginLoadFailed);
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));

    const int discovered = manager.discover();
    QVERIFY2(discovered == 1,
             qPrintable(failures.isEmpty()
                            ? QStringLiteral("No package discovery error was emitted")
                            : failures.constFirst().at(1).toString()));
    QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture")));

    {
        const PluginLease lease = manager.acquire(QStringLiteral("org.quemusic.source.fixture"));
        QVERIFY(lease.isValid());
        QCOMPARE(manager.unload(QStringLiteral("org.quemusic.source.fixture")),
                 PluginOperationResult::Busy);
    }
    QCOMPARE(manager.unload(QStringLiteral("org.quemusic.source.fixture")),
             PluginOperationResult::Success);
}

void PluginManagerTest::reloadsAfterFinalLeaseIsReleased()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture")));
    {
        const PluginLease lease = manager.acquire(QStringLiteral("org.quemusic.source.fixture"));
        QVERIFY(lease.isValid());
    }

    const PluginOperationResult reloadResult =
        manager.reload(QStringLiteral("org.quemusic.source.fixture"));
    QVERIFY2(reloadResult == PluginOperationResult::Success,
             qPrintable(manager.plugin(QStringLiteral("org.quemusic.source.fixture")).error));
    QCOMPARE(manager.plugin(QStringLiteral("org.quemusic.source.fixture")).state,
             PluginState::Loaded);
}

void PluginManagerTest::malformedPackageDoesNotBlockValidPackage()
{
    const QString root = QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR);
    const QString brokenDirectory = QDir(root).filePath(QStringLiteral("broken"));
    QDir(brokenDirectory).removeRecursively();
    QVERIFY(QDir().mkpath(brokenDirectory));

    QFile manifest(QDir(brokenDirectory).filePath(QStringLiteral("manifest.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write("{}");
    manifest.close();

    PluginManager manager;
    QSignalSpy failures(&manager, &PluginManager::pluginLoadFailed);
    manager.addSearchPath(root);

    QCOMPARE(manager.discover(), 1);
    QCOMPARE(failures.count(), 1);
    QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture")));

    QVERIFY(QDir(brokenDirectory).removeRecursively());
}

QTEST_MAIN(PluginManagerTest)
#include "tst_PluginManager.moc"
