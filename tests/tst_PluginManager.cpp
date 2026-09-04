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
    void enforcesCanonicalArchitectureCompatibility_data();
    void enforcesCanonicalArchitectureCompatibility();
    void usesQtBuildArchitectureForNativeCompatibility();
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
    void healthyLeasesOutliveFacadeAndReleaseLastRoot();
    void leaseReleaseDoesNotNotifyPartlyDestroyedFacade();
    void releaseObserverMayDestroyFacade();
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
    return QStringLiteral(QUEMUSIC_TEST_BUILD_KEY);
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
                                 QSysInfo::buildCpuArchitecture()},
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
    QTest::newRow("build-key") << QStringLiteral("buildKey")
                               << QJsonValue(hostBuildKey() == QStringLiteral("Debug")
                                                 ? QStringLiteral("Release")
                                                 : QStringLiteral("Debug"))
                               << QStringLiteral("build key");
}

void PluginManagerTest::enforcesCanonicalArchitectureCompatibility_data()
{
    QTest::addColumn<QString>("declaredArchitecture");
    QTest::addColumn<bool>("compatible");

    const QString host = QSysInfo::buildCpuArchitecture().trimmed().toLower();
    if (host == QStringLiteral("x86_64") || host == QStringLiteral("amd64")) {
        QTest::newRow("host-amd64-alias") << QStringLiteral("AMD64") << true;
        QTest::newRow("host-x86-64-canonical") << QStringLiteral("x86_64") << true;
        QTest::newRow("opposite-aarch64-alias") << QStringLiteral("aarch64") << false;
        QTest::newRow("opposite-arm64-canonical") << QStringLiteral("arm64") << false;
    } else if (host == QStringLiteral("arm64") || host == QStringLiteral("aarch64")) {
        QTest::newRow("host-aarch64-alias") << QStringLiteral("aarch64") << true;
        QTest::newRow("host-arm64-canonical") << QStringLiteral("arm64") << true;
        QTest::newRow("opposite-amd64-alias") << QStringLiteral("AMD64") << false;
        QTest::newRow("opposite-x86-64-canonical") << QStringLiteral("x86_64") << false;
    } else {
        QSKIP(qPrintable(QStringLiteral("Unsupported test host architecture: %1").arg(host)));
    }
    QTest::newRow("universal-package") << QStringLiteral("universal") << true;
}

void PluginManagerTest::enforcesCanonicalArchitectureCompatibility()
{
    QFETCH(QString, declaredArchitecture);
    QFETCH(bool, compatible);

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString packagePath = QDir(root.path()).filePath(QStringLiteral("fixture-v2"));
    copyPackage(QDir(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR))
                    .filePath(QStringLiteral("fixture-v2")),
                packagePath);
    replaceRuntimeRequirement(packagePath, QStringLiteral("architecture"),
                              declaredArchitecture);

    PluginManager manager;
    manager.addSearchPath(root.path());
    QCOMPARE(manager.discover(), 1);
    QCOMPARE(manager.load(QStringLiteral("org.quemusic.source.fixture-v2")), compatible);
    const PluginSpec spec = manager.plugin(QStringLiteral("org.quemusic.source.fixture-v2"));
    if (compatible) {
        QCOMPARE(spec.state, PluginState::Loaded);
    } else {
        QCOMPARE(spec.state, PluginState::Failed);
        QVERIFY2(spec.error.contains(QStringLiteral("architecture"), Qt::CaseInsensitive),
                 qPrintable(spec.error));
    }
}

void PluginManagerTest::usesQtBuildArchitectureForNativeCompatibility()
{
    const QString sourcePath = QFINDTESTDATA("../core/plugins/PluginManager.cpp");
    QVERIFY2(!sourcePath.isEmpty(), "PluginManager.cpp test data was not found");

    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray source = sourceFile.readAll();

    QVERIFY2(source.contains("QSysInfo::buildCpuArchitecture()"),
             "Native plugin compatibility must use the Qt/process build architecture");
    QVERIFY2(!source.contains("QSysInfo::currentCpuArchitecture()"),
             "OS/native CPU architecture is wrong under translation or emulation");
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

void PluginManagerTest::healthyLeasesOutliveFacadeAndReleaseLastRoot()
{
    const QString packageId = QStringLiteral("org.quemusic.source.fixture-v2");
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(manager->discover(), 1); QVERIFY(manager->load(packageId));
    QPointer<QObject> root = manager->pluginInstance(packageId);
    auto first = manager->acquire(packageId);
    auto last = manager->acquire(packageId);
    auto copy = last;
    QCOMPARE(manager->plugin(packageId).activeLeases, 2);
    manager.reset();
    // No session or plugin stack in RED; inspect QPointer before invoking code.
    QVERIFY2(root, "Callable leases must keep the root after facade destruction");
    QCOMPARE(root->property("sourceSdkAbi").toInt(), 2);
    first = {}; last = {};
    QVERIFY(root);
    copy = {};
    QVERIFY2(root.isNull(), "The last healthy lease must actually destroy the root, not permanently pin it");
    PluginManager replacement;
    replacement.addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(replacement.discover(), 1); QVERIFY(replacement.load(packageId));
    QCOMPARE(replacement.reload(packageId), PluginOperationResult::Success);
    QCOMPARE(replacement.unload(packageId), PluginOperationResult::Success);
}

void PluginManagerTest::leaseReleaseDoesNotNotifyPartlyDestroyedFacade()
{
    const QString firstId = QStringLiteral("org.quemusic.source.fixture");
    const QString secondId = QStringLiteral("org.quemusic.source.fixture-v2");
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    manager->addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(manager->discover(), 2);
    QVERIFY(manager->load(firstId)); QVERIFY(manager->load(secondId));
    auto lease = manager->acquire(secondId);
    QPointer<QObject> secondRoot = manager->pluginInstance(secondId);
    bool destroying = false;
    int lateNotifications = 0;
    QObject observer;
    connect(manager.get(), &PluginManager::pluginsChanged, &observer, [&] {
        if (destroying) ++lateNotifications;
    });
    connect(manager->pluginInstance(firstId), &QObject::destroyed, &observer, [&] {
        destroying = true;
        lease = {};
    });
    manager.reset();
    QVERIFY(destroying);
    QCOMPARE(lateNotifications, 0);
    QVERIFY(secondRoot.isNull());
}

void PluginManagerTest::releaseObserverMayDestroyFacade()
{
    const QString packageId = QStringLiteral("org.quemusic.source.fixture-v2");
    auto manager = std::make_unique<PluginManager>();
    manager->addSearchPath(QStringLiteral(QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(manager->discover(), 1); QVERIFY(manager->load(packageId));
    auto lease = manager->acquire(packageId);
    QPointer<QObject> root = manager->pluginInstance(packageId);
    QObject observer;
    bool retainedDuringRelease = false;
    connect(manager.get(), &PluginManager::pluginsChanged, &observer, [&] {
        manager.reset();
        retainedDuringRelease = !root.isNull();
    });
    lease = {};
    QVERIFY2(retainedDuringRelease, "A release notification must retain its callable loader across facade deletion");
    QVERIFY(root.isNull());
}

QTEST_MAIN(PluginManagerTest)
#include "tst_PluginManager.moc"
