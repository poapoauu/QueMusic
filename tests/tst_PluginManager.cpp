#include "PluginManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PluginManagerTest : public QObject {
    Q_OBJECT

private slots:
    void refusesUnloadWhileLeaseIsActive();
    void reloadsAfterFinalLeaseIsReleased();
    void malformedPackageDoesNotBlockValidPackage();
    void rejectsDuplicateSourceIdBeforeLoad();
};

namespace {

void copyPackage(const QString &sourcePath, const QString &destinationPath)
{
    QVERIFY(QDir().mkpath(destinationPath));
    const QDir source(sourcePath);
    for (const QString &fileName : source.entryList(QDir::Files)) {
        QVERIFY(QFile::copy(source.filePath(fileName),
                            QDir(destinationPath).filePath(fileName)));
    }
}

}

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

void PluginManagerTest::rejectsDuplicateSourceIdBeforeLoad()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString fixturePath = QDir(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR))
                                    .filePath(QStringLiteral("fixture"));
    const QString firstPackage = QDir(root.path()).filePath(QStringLiteral("accepted"));
    const QString duplicatePackage = QDir(root.path()).filePath(QStringLiteral("rejected"));
    copyPackage(fixturePath, firstPackage);
    copyPackage(fixturePath, duplicatePackage);

    QFile duplicateManifest(QDir(duplicatePackage).filePath(QStringLiteral("manifest.json")));
    QVERIFY(duplicateManifest.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(duplicateManifest.readAll()).object();
    duplicateManifest.close();
    manifest.insert(QStringLiteral("id"), QStringLiteral("org.quemusic.source.duplicate"));
    duplicateManifest.setFileName(QDir(duplicatePackage).filePath(QStringLiteral("manifest.json")));
    QVERIFY(duplicateManifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    duplicateManifest.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    duplicateManifest.close();

    PluginManager manager;
    manager.addSearchPath(root.path());
    QCOMPARE(manager.discover(), 1);
    QCOMPARE(manager.plugins().size(), 1);
    QVERIFY(!manager.load(QStringLiteral("org.quemusic.source.duplicate")));
}

QTEST_MAIN(PluginManagerTest)
#include "tst_PluginManager.moc"
