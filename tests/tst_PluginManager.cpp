#include "PluginManager.h"
#include "PluginManifest.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QPointer>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

class PluginManagerTest : public QObject {
    Q_OBJECT

private slots:
    void loadsV1AndV2AccordingToManifestAbi();
    void rejectsRuntimeMismatchBeforeLoadingV2_data();
    void rejectsRuntimeMismatchBeforeLoadingV2();
    void rejectsPluginMetadataIidThatDisagreesWithManifest();
    void rejectsV2PluginThatDoesNotImplementDeclaredInterface();
    void refusesUnloadWhileLeaseIsActive();
    void refusesUnloadWhileV2SessionLeaseExists();
    void reloadsAfterFinalLeaseIsReleased();
    void repeatedReloadDoesNotRetainV2InstancesOrLeases();
    void malformedPackageDoesNotBlockValidPackage();
    void rejectsDuplicateSourceIdBeforeLoad();
    void failedActivationPreservesLoaderLifecycle();
    void failedUnloadExposesRetryCapability();
    void destructionReleasesOwnedPluginLoaders();
    void rejectsLoadWhilePackageOwnsLoader();
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

QString hostBuildKey()
{
#ifdef QT_NO_DEBUG
    return QStringLiteral("Release");
#else
    return QStringLiteral("Debug");
#endif
}

void replaceRuntimeRequirement(const QString &packagePath, const QString &key,
                               const QJsonValue &value)
{
    const QString manifestPath = QDir(packagePath).filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    QJsonObject requirements = manifest.value(QStringLiteral("runtimeRequirements")).toObject();
    requirements.insert(key, value);
    manifest.insert(QStringLiteral("runtimeRequirements"), requirements);
    QVERIFY(manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

void rewriteV1PackageAsV2(const QString &packagePath)
{
    const QString manifestPath = QDir(packagePath).filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest.insert(QStringLiteral("id"), QStringLiteral("org.quemusic.source.fixture-v2-iid-mismatch"));
    manifest.insert(QStringLiteral("sourceId"), QStringLiteral("fixture-v2-iid-mismatch"));
    manifest.insert(
        QStringLiteral("interfaces"),
        QJsonArray{QJsonObject{{QStringLiteral("id"),
                                QStringLiteral("org.quemusic.MusicSourcePlugin/2.0")},
                               {QStringLiteral("version"), QStringLiteral("2.0")}}});
    manifest.insert(QStringLiteral("runtimeRequirements"),
                    QJsonObject{{QStringLiteral("sourceSdkAbi"), 2},
                                {QStringLiteral("qtMajor"), QT_VERSION_MAJOR},
                                {QStringLiteral("architecture"),
                                 QSysInfo::currentCpuArchitecture()},
                                {QStringLiteral("buildKey"), hostBuildKey()}});
    QVERIFY(manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

}

void PluginManagerTest::loadsV1AndV2AccordingToManifestAbi()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 2);
    QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture")));
    QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture-v2")));
    QCOMPARE(manager.plugin(QStringLiteral("org.quemusic.source.fixture")).state,
             PluginState::Loaded);
    QObject *v2Instance =
        manager.pluginInstance(QStringLiteral("org.quemusic.source.fixture-v2"));
    QVERIFY(v2Instance != nullptr);
    QCOMPARE(v2Instance->property("sourceSdkAbi").toInt(), 2);
}

void PluginManagerTest::rejectsRuntimeMismatchBeforeLoadingV2_data()
{
    QTest::addColumn<QString>("requirement");
    QTest::addColumn<QJsonValue>("value");
    QTest::addColumn<QString>("errorFragment");

    QTest::newRow("qt-major") << QStringLiteral("qtMajor")
                               << QJsonValue(QT_VERSION_MAJOR + 1)
                               << QStringLiteral("Qt major");
    QTest::newRow("architecture") << QStringLiteral("architecture")
                                  << QJsonValue(QStringLiteral("not-the-host-architecture"))
                                  << QStringLiteral("architecture");
    QTest::newRow("build-key") << QStringLiteral("buildKey")
                               << QJsonValue(hostBuildKey() == QStringLiteral("Debug")
                                                 ? QStringLiteral("Release")
                                                 : QStringLiteral("Debug"))
                               << QStringLiteral("build key");
}

void PluginManagerTest::rejectsRuntimeMismatchBeforeLoadingV2()
{
    QFETCH(QString, requirement);
    QFETCH(QJsonValue, value);
    QFETCH(QString, errorFragment);

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString packagePath = QDir(root.path()).filePath(QStringLiteral("fixture-v2"));
    copyPackage(QDir(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR))
                    .filePath(QStringLiteral("fixture-v2")),
                packagePath);
    replaceRuntimeRequirement(packagePath, requirement, value);

    PluginManager manager;
    manager.addSearchPath(root.path());
    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture-v2")) == false);
    const PluginSpec spec = manager.plugin(QStringLiteral("org.quemusic.source.fixture-v2"));
    QCOMPARE(spec.state, PluginState::Failed);
    QVERIFY2(spec.error.contains(errorFragment, Qt::CaseInsensitive), qPrintable(spec.error));
    QVERIFY(manager.pluginInstance(QStringLiteral("org.quemusic.source.fixture-v2")) == nullptr);
}

void PluginManagerTest::rejectsV2PluginThatDoesNotImplementDeclaredInterface()
{
    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_INVALID_V2_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 1);
    const QString packageId = QStringLiteral("org.quemusic.source.fixture-v2-invalid");
    QVERIFY(manager.load(packageId) == false);
    const PluginSpec spec = manager.plugin(packageId);
    QCOMPARE(spec.state, PluginState::Failed);
    QVERIFY2(spec.error.contains(QStringLiteral("IMusicSourcePluginV2")), qPrintable(spec.error));
    QVERIFY(manager.pluginInstance(packageId) == nullptr);
}

void PluginManagerTest::rejectsPluginMetadataIidThatDisagreesWithManifest()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString packagePath = QDir(root.path()).filePath(QStringLiteral("iid-mismatch"));
    copyPackage(QDir(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR))
                    .filePath(QStringLiteral("fixture")),
                packagePath);
    rewriteV1PackageAsV2(packagePath);

    PluginManager manager;
    manager.addSearchPath(root.path());
    QCOMPARE(manager.discover(), 1);
    const QString packageId = QStringLiteral("org.quemusic.source.fixture-v2-iid-mismatch");
    QVERIFY(manager.load(packageId) == false);
    const PluginSpec spec = manager.plugin(packageId);
    QCOMPARE(spec.state, PluginState::Failed);
    QVERIFY2(spec.error.contains(QStringLiteral("metadata IID")), qPrintable(spec.error));
    QVERIFY(spec.error.contains(QStringLiteral("MusicSourcePlugin/1.0")));
    QVERIFY(spec.error.contains(QStringLiteral("MusicSourcePlugin/2.0")));
    QVERIFY(manager.pluginInstance(packageId) == nullptr);
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
        QCOMPARE(manager.plugin(QStringLiteral("org.quemusic.source.fixture")).busyReason,
                 QStringLiteral("source.sessions.active"));
    }
    QVERIFY(manager.plugin(QStringLiteral("org.quemusic.source.fixture")).busyReason.isEmpty());
    QCOMPARE(manager.unload(QStringLiteral("org.quemusic.source.fixture")),
             PluginOperationResult::Success);
}

void PluginManagerTest::refusesUnloadWhileV2SessionLeaseExists()
{
    PluginManager manager;
    const QString packageId = QStringLiteral("org.quemusic.source.fixture-v2");
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(packageId));
    PluginLease firstLease = manager.acquire(packageId);
    PluginLease finalLease = manager.acquire(packageId);
    QCOMPARE(manager.unload(packageId), PluginOperationResult::Busy);
    QCOMPARE(manager.plugin(packageId).state, PluginState::Loaded);
    QCOMPARE(manager.plugin(packageId).busyReason, QStringLiteral("source.sessions.active"));

    firstLease = {};
    QCOMPARE(manager.plugin(packageId).activeLeases, 1);
    QCOMPARE(manager.plugin(packageId).busyReason, QStringLiteral("source.sessions.active"));
    finalLease = {};
    QCOMPARE(manager.plugin(packageId).activeLeases, 0);
    QVERIFY(manager.plugin(packageId).busyReason.isEmpty());
    QCOMPARE(manager.unload(packageId), PluginOperationResult::Success);
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

void PluginManagerTest::repeatedReloadDoesNotRetainV2InstancesOrLeases()
{
    PluginManager manager;
    const QString packageId = QStringLiteral("org.quemusic.source.fixture-v2");
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(packageId));
    for (int iteration = 0; iteration < 50; ++iteration) {
        QPointer<QObject> previousInstance = manager.pluginInstance(packageId);
        QVERIFY(previousInstance != nullptr);
        QCOMPARE(previousInstance->property("sourceSdkAbi").toInt(), 2);

        QCOMPARE(manager.reload(packageId), PluginOperationResult::Success);
        QVERIFY2(previousInstance.isNull(), "Reload retained the previous v2 plugin instance");
        QVERIFY(manager.pluginInstance(packageId) != nullptr);
        QCOMPARE(manager.plugin(packageId).state, PluginState::Loaded);
        QCOMPARE(manager.plugin(packageId).activeLeases, 0);
        QVERIFY(manager.plugin(packageId).busyReason.isEmpty());
    }
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

void PluginManagerTest::failedActivationPreservesLoaderLifecycle()
{
    PluginManager manager;
    const QString packageId = QStringLiteral("org.quemusic.source.fixture");
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(packageId));
    const bool released = manager.failLoadedPlugin(packageId, QStringLiteral("Activation failed"));
    if (released) {
        QCOMPARE(manager.plugin(packageId).state, PluginState::Failed);
        QVERIFY(manager.plugin(packageId).error.contains(QStringLiteral("Activation failed")));
        QVERIFY(manager.pluginInstance(packageId) == nullptr);
        QVERIFY(manager.load(packageId));
        return;
    }

    const QString activationError = manager.plugin(packageId).error;
    QCOMPARE(manager.plugin(packageId).state, PluginState::Failed);
    QVERIFY(activationError.contains(QStringLiteral("Activation failed")));
    QVERIFY(!manager.load(packageId));
    const PluginOperationResult retryUnload = manager.unload(packageId);
    if (retryUnload == PluginOperationResult::Success) {
        QVERIFY(manager.load(packageId));
    } else {
        QCOMPARE(retryUnload, PluginOperationResult::Failed);
        QVERIFY(manager.plugin(packageId).error != activationError);
    }
}

void PluginManagerTest::failedUnloadExposesRetryCapability()
{
    const QString packageId = QStringLiteral("org.quemusic.source.fixture");
    const QString manifestPath =
        QDir(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR))
            .filePath(QStringLiteral("fixture/manifest.json"));
    QString manifestError;
    const PluginManifest manifest = PluginManifest::fromFile(manifestPath, &manifestError);
    QVERIFY2(manifest.isValid(), qPrintable(manifestError));

    QPluginLoader retainedReference;
    retainedReference.setLoadHints({});
    retainedReference.setFileName(manifest.libraryAbsolutePath());
    QCOMPARE(retainedReference.loadHints(), QLibrary::LoadHints{});
    QVERIFY2(retainedReference.load(), qPrintable(retainedReference.errorString()));

    PluginManager manager;
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(packageId));

    QVERIFY(!manager.failLoadedPlugin(packageId, QStringLiteral("Activation failed")));

    const PluginSpec failed = manager.plugin(packageId);
    QCOMPARE(failed.state, PluginState::Failed);
    QCOMPARE(failed.activeLeases, 0);
    QVERIFY(!manager.load(packageId));

    const QVariantMap visiblePackage = manager.plugins().constFirst().toMap();
    QCOMPARE(visiblePackage.value(QStringLiteral("loadable")).toBool(), false);
    QCOMPARE(visiblePackage.value(QStringLiteral("unloadable")).toBool(), true);

    QVERIFY2(retainedReference.unload(), qPrintable(retainedReference.errorString()));

    QVERIFY2(manager.unload(packageId) == PluginOperationResult::Success,
             qPrintable(manager.plugin(packageId).error));
    QCOMPARE(manager.plugin(packageId).state, PluginState::Unloaded);

    const QVariantMap unloadedPackage = manager.plugins().constFirst().toMap();
    QCOMPARE(unloadedPackage.value(QStringLiteral("state")).toString(),
             QStringLiteral("unloaded"));
    QCOMPARE(unloadedPackage.value(QStringLiteral("loadable")).toBool(), true);
    QCOMPARE(unloadedPackage.value(QStringLiteral("unloadable")).toBool(), false);
    QVERIFY(manager.load(packageId));
}

void PluginManagerTest::destructionReleasesOwnedPluginLoaders()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString packagePath = QDir(root.path()).filePath(QStringLiteral("fixture"));
    copyPackage(QDir(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR))
                    .filePath(QStringLiteral("fixture")),
                packagePath);

    const QString manifestPath = QDir(packagePath).filePath(QStringLiteral("manifest.json"));
    QString manifestError;
    const PluginManifest manifest = PluginManifest::fromFile(manifestPath, &manifestError);
    QVERIFY2(manifest.isValid(), qPrintable(manifestError));

    {
        PluginManager manager;
        manager.addSearchPath(root.path());
        QCOMPARE(manager.discover(), 1);
        QVERIFY(manager.load(QStringLiteral("org.quemusic.source.fixture")));
    }

    QPluginLoader probe;
    probe.setLoadHints({});
    probe.setFileName(manifest.libraryAbsolutePath());
    QVERIFY2(probe.load(), qPrintable(probe.errorString()));
    QVERIFY2(probe.unload(), qPrintable(probe.errorString()));
}

void PluginManagerTest::rejectsLoadWhilePackageOwnsLoader()
{
    PluginManager manager;
    const QString packageId = QStringLiteral("org.quemusic.source.fixture");
    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));

    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load(packageId));
    QVERIFY(!manager.load(packageId));
    QCOMPARE(manager.plugin(packageId).state, PluginState::Loaded);
}

QTEST_MAIN(PluginManagerTest)
#include "tst_PluginManager.moc"
