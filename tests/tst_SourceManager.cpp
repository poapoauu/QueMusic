#include "IMusicSourceArtworkSession.h"
#include "PluginManager.h"
#include "SourceManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class SourceManagerTest : public QObject {
    Q_OBJECT

private slots:
    void loadsValidSourcePlugin();
    void ignoresMissingSearchPath();
    void rejectsUnknownSourceId();
    void rejectsDuplicateSourceIdWithoutStoppingOtherSources();
    void rejectsInvalidPluginsWithoutStoppingOtherSources();
    void rejectsUnsupportedSdkVersionWithoutStoppingOtherSources();
    void fakeSourceReturnsNamespacedTrack();
    void fakeSourceCancellationSuppressesResult();
    void fakeSourceCompletesStreamResolution();
    void fakeSourceCompletesArtworkFetch();
    void baseOnlyFixtureDoesNotExposeArtworkInterface();
    void legacyArtworkMetadataFallsBackToV1BaseSession();
    void optionalArtworkInterfaceWithoutMetadataIsNotUsed();
    void sourceSessionKeepsNativePackageLoaded();
    void nativePackageLifecycleKeepsSourceRegistryInSync();
    void failedSourceInitializationMarksPackageFailed();
    void forwardsPackageActivationFailureToSourceLoadFailed();
    void failedSourceActivationLeavesPackageFailed();
    void destroyedPluginManagerRemovesPackageSources();
    void managerlessSourceManagerCannotLoadNativePlugins();
    void pluginManagerLoadReportsSourceActivationFailure();
};

namespace {

QString fixtureDirectory(const QString &name)
{
    return QDir(QStringLiteral(QUEMUSIC_TEST_PLUGIN_ROOT)).filePath(name);
}

QString testSourcePluginDirectory()
{
    return QStringLiteral(QUEMUSIC_TEST_SOURCE_PLUGIN_DIR);
}

QString frozenV1PluginDirectory()
{
    return QStringLiteral(QUEMUSIC_TEST_FROZEN_V1_PLUGIN_DIR);
}

QString initializationFailurePackage(QTemporaryDir *root, bool *manifestWritten)
{
    *manifestWritten = false;
    if (!root->isValid()) {
        return {};
    }
    const QDir sourceDirectory(fixtureDirectory(QStringLiteral("invalid")));
    const QStringList libraries = sourceDirectory.entryList(QDir::Files);
    const auto library = std::find_if(libraries.cbegin(), libraries.cend(),
                                      [](const QString &name) {
                                          return name.contains(
                                              QStringLiteral("initialization_failure"));
                                      });
    if (library == libraries.cend()) {
        return {};
    }
    const QString libraryName = *library;

    const QString packageDirectory = QDir(root->path()).filePath(QStringLiteral("package"));
    if (!QDir().mkpath(packageDirectory)
        || !QFile::copy(sourceDirectory.filePath(libraryName),
                        QDir(packageDirectory).filePath(libraryName))) {
        return {};
    }

    QFile manifest(QDir(packageDirectory).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::WriteOnly)) {
        return {};
    }
    const QByteArray contents = QStringLiteral(R"({"id":"org.quemusic.source.initialization-failure","sourceId":"fixture.initialize-failure","name":"Initialization Failure Source","version":"1.0.0","category":"source","runtime":"native-qt","library":"%1","pluginApi":{"major":1,"minHostMinor":0},"interfaces":[{"id":"org.quemusic.MusicSourcePlugin/1.0","version":"1.0"}],"runtimeRequirements":{"qtMajor":6}})")
                                    .arg(libraryName)
                                    .toUtf8();
    *manifestWritten = manifest.write(contents) == contents.size();
    manifest.close();
    return *manifestWritten ? root->path() : QString();
}

class SourceManagerHarness {
public:
    explicit SourceManagerHarness(bool includePrimaryPackage = true)
        : manager(&plugins)
    {
        if (includePrimaryPackage) {
            primaryPackageReady = addPackage(testSourcePluginDirectory(),
                                             QStringLiteral("test_source"),
                                             QStringLiteral("org.quemusic.source.fixture"),
                                             QStringLiteral("test-source"),
                                             QStringLiteral("Test Source"));
        }
    }

    bool addPackage(const QString &libraryDirectory, const QString &libraryFragment,
                    const QString &packageId, const QString &sourceId, const QString &name)
    {
        if (!root.isValid()) {
            return false;
        }
        const QDir sourceDirectory(libraryDirectory);
        const QStringList libraries = sourceDirectory.entryList(QDir::Files);
        const auto library = std::find_if(libraries.cbegin(), libraries.cend(),
                                          [&libraryFragment](const QString &candidate) {
                                              return candidate.contains(libraryFragment);
                                          });
        if (library == libraries.cend()) {
            return false;
        }

        const QString packageDirectory = QDir(root.path()).filePath(
            QStringLiteral("package-%1").arg(nextPackage++));
        if (!QDir().mkpath(packageDirectory)
            || !QFile::copy(sourceDirectory.filePath(*library),
                            QDir(packageDirectory).filePath(*library))) {
            return false;
        }

        const QByteArray manifestContents = QStringLiteral(
            R"({"id":"%1","sourceId":"%2","name":"%3","version":"1.0.0","category":"source","runtime":"native-qt","library":"%4","pluginApi":{"major":1,"minHostMinor":0},"interfaces":[{"id":"org.quemusic.MusicSourcePlugin/1.0","version":"1.0"}],"runtimeRequirements":{"qtMajor":6}})")
                                               .arg(packageId, sourceId, name, *library)
                                               .toUtf8();
        QFile manifest(QDir(packageDirectory).filePath(QStringLiteral("manifest.json")));
        if (!manifest.open(QIODevice::WriteOnly)) {
            return false;
        }
        const bool written = manifest.write(manifestContents) == manifestContents.size();
        manifest.close();
        if (written) {
            plugins.addSearchPath(root.path());
        }
        return written;
    }

    QTemporaryDir root;
    PluginManager plugins;
    SourceManager manager;
    bool primaryPackageReady = true;

private:
    int nextPackage = 0;
};

}

void SourceManagerTest::loadsValidSourcePlugin()
{
    SourceManagerHarness harness;
    SourceManager &manager = harness.manager;
    QSignalSpy loaded(&manager, &SourceManager::sourceLoaded);
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};
    QObject parent;

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(loaded.count(), 1);
    const QVariantList sources = manager.availableSources();
    QCOMPARE(sources.size(), 1);
    const QVariantMap source = sources.constFirst().toMap();
    QCOMPARE(source.value(QStringLiteral("id")).toString(), QStringLiteral("test-source"));
    QCOMPARE(source.value(QStringLiteral("name")).toString(), QStringLiteral("Test Source"));
    QCOMPARE(source.value(QStringLiteral("version")).toString(), QStringLiteral("1.0.0"));
    QCOMPARE(source.value(QStringLiteral("protocol")).toString(), QStringLiteral("test"));
    QCOMPARE(source.value(QStringLiteral("capabilities")).toULongLong(), qulonglong(21));
    QVERIFY(source.value(QStringLiteral("capabilities")).toULongLong() &
            static_cast<qulonglong>(SourceCapability::Artwork));

    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &parent);
}

void SourceManagerTest::ignoresMissingSearchPath()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;

    harness.plugins.addSearchPath(fixtureDirectory(QStringLiteral("does-not-exist")));

    QCOMPARE(manager.loadAll(), 0);
    QCOMPARE(manager.sourceIds(), QStringList());

    QVERIFY(harness.addPackage(testSourcePluginDirectory(), QStringLiteral("test_source"),
                               QStringLiteral("org.quemusic.source.fixture"),
                               QStringLiteral("test-source"), QStringLiteral("Test Source")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("test-source")}));
}

void SourceManagerTest::rejectsUnknownSourceId()
{
    SourceManagerHarness harness;
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("unknown"), QStringLiteral("account-1"),
                                QStringLiteral("Unknown Account")};

    QCOMPARE(manager.loadAll(), 1);

    QVERIFY(manager.createSession(account.sourceId, account, &parent) == nullptr);
}

void SourceManagerTest::rejectsDuplicateSourceIdWithoutStoppingOtherSources()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.duplicate"), QStringLiteral("account-1"),
                                QStringLiteral("Duplicate Account")};

    QVERIFY(harness.addPackage(fixtureDirectory(QStringLiteral("duplicate")),
                               QStringLiteral("fixture_one"),
                               QStringLiteral("org.quemusic.source.duplicate-one"),
                               account.sourceId, QStringLiteral("Duplicate Source")));
    QVERIFY(harness.addPackage(fixtureDirectory(QStringLiteral("duplicate")),
                               QStringLiteral("fixture_two"),
                               QStringLiteral("org.quemusic.source.duplicate-two"),
                               account.sourceId, QStringLiteral("Duplicate Source")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.duplicate")}));
    QVERIFY(manager.createSession(account.sourceId, account, &parent) != nullptr);
}

void SourceManagerTest::rejectsInvalidPluginsWithoutStoppingOtherSources()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;

    const QString invalidDirectory = fixtureDirectory(QStringLiteral("invalid"));
    QVERIFY(harness.addPackage(invalidDirectory, QStringLiteral("invalid_valid"),
                               QStringLiteral("org.quemusic.source.valid"),
                               QStringLiteral("fixture.valid"), QStringLiteral("Fixture Source")));
    QVERIFY(harness.addPackage(invalidDirectory, QStringLiteral("empty_id"),
                               QStringLiteral("org.quemusic.source.empty-id"),
                               QStringLiteral("manifest.empty-id"), QStringLiteral("Empty ID Source")));
    QVERIFY(harness.addPackage(invalidDirectory, QStringLiteral("empty_sdk"),
                               QStringLiteral("org.quemusic.source.empty-sdk"),
                               QStringLiteral("fixture.empty-sdk"), QStringLiteral("Empty SDK Source")));
    QVERIFY(harness.addPackage(invalidDirectory, QStringLiteral("missing_name"),
                               QStringLiteral("org.quemusic.source.missing-name"),
                               QStringLiteral("fixture.missing-name"), QStringLiteral("Missing Name Source")));
    QVERIFY(harness.addPackage(invalidDirectory, QStringLiteral("initialization_failure"),
                               QStringLiteral("org.quemusic.source.initialization-failure"),
                               QStringLiteral("fixture.initialize-failure"),
                               QStringLiteral("Initialization Failure Source")));
    QVERIFY(harness.addPackage(invalidDirectory, QStringLiteral("not_source"),
                               QStringLiteral("org.quemusic.source.not-source"),
                               QStringLiteral("fixture.not-source"), QStringLiteral("Not Source")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.valid")}));
}

void SourceManagerTest::rejectsUnsupportedSdkVersionWithoutStoppingOtherSources()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;

    QVERIFY(harness.addPackage(fixtureDirectory(QStringLiteral("incompatible")),
                               QStringLiteral("incompatible_sdk"),
                               QStringLiteral("org.quemusic.source.incompatible"),
                               QStringLiteral("fixture.incompatible-sdk"),
                               QStringLiteral("Incompatible SDK Source")));
    QVERIFY(harness.addPackage(testSourcePluginDirectory(), QStringLiteral("test_source"),
                               QStringLiteral("org.quemusic.source.fixture"),
                               QStringLiteral("test-source"), QStringLiteral("Test Source")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("test-source")}));
}

void SourceManagerTest::fakeSourceReturnsNamespacedTrack()
{
    SourceManagerHarness harness;
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    QCOMPARE(manager.loadAll(), 1);
    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);

    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const QUuid requestId = session->search({QStringLiteral("anything"), 1});

    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.count(), 1);
    const QList<QVariant> arguments = succeeded.constFirst();
    QCOMPARE(arguments.at(0).toUuid(), requestId);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("search"));
    const QJsonArray tracks = arguments.at(2).toJsonValue().toArray();
    QCOMPARE(tracks.size(), 1);
    const QJsonObject track = tracks.at(0).toObject();
    QCOMPARE(track.value(QStringLiteral("sourceId")).toString(), QStringLiteral("test-source"));
    QCOMPARE(track.value(QStringLiteral("nativeId")).toString(), QStringLiteral("test-track-1"));
}

void SourceManagerTest::fakeSourceCancellationSuppressesResult()
{
    SourceManagerHarness harness;
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    QCOMPARE(manager.loadAll(), 1);
    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);

    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const QUuid requestId = session->search({QStringLiteral("anything"), 1});
    session->cancel(requestId);

    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
}

void SourceManagerTest::fakeSourceCompletesStreamResolution()
{
    SourceManagerHarness harness;
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    QCOMPARE(manager.loadAll(), 1);
    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);

    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{QStringLiteral("test-source"), QStringLiteral("test-track-1")};
    const QUuid requestId = session->resolveStream(track);

    QVERIFY(succeeded.wait(1000));
    const QList<QVariant> arguments = succeeded.constFirst();
    QCOMPARE(arguments.at(0).toUuid(), requestId);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("resolveStream"));
    const QJsonObject stream = arguments.at(2).toJsonValue().toObject();
    QCOMPARE(stream.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.invalid/test-track-1.mp3"));
    QCOMPARE(stream.value(QStringLiteral("mimeType")).toString(), QStringLiteral("audio/mpeg"));
}

void SourceManagerTest::fakeSourceCompletesArtworkFetch()
{
    SourceManagerHarness harness;
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    QCOMPARE(manager.loadAll(), 1);
    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    auto *artworkSession = qobject_cast<IMusicSourceArtworkSession *>(session);
    QVERIFY(artworkSession != nullptr);

    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{QStringLiteral("test-source"), QStringLiteral("test-track-1")};
    const QUuid requestId = manager.requestArtwork(account.sourceId, session, track);

    QVERIFY(succeeded.wait(1000));
    const QList<QVariant> arguments = succeeded.constFirst();
    QCOMPARE(arguments.at(0).toUuid(), requestId);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("fetchArtwork"));
    const QJsonObject artwork = arguments.at(2).toJsonValue().toObject();
    QCOMPARE(artwork.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.invalid/test-track-1.png"));
    QCOMPARE(artwork.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
}

void SourceManagerTest::baseOnlyFixtureDoesNotExposeArtworkInterface()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.valid"), QStringLiteral("account-1"),
                                QStringLiteral("Fixture Account")};

    QVERIFY(harness.addPackage(fixtureDirectory(QStringLiteral("invalid")),
                               QStringLiteral("invalid_valid"),
                               QStringLiteral("org.quemusic.source.valid"), account.sourceId,
                               QStringLiteral("Fixture Source")));
    QCOMPARE(manager.loadAll(), 1);

    const QVariantMap source = manager.availableSources().constFirst().toMap();
    QCOMPARE(source.value(QStringLiteral("capabilities")).toULongLong(), qulonglong(1));
    QVERIFY((source.value(QStringLiteral("capabilities")).toULongLong() &
             static_cast<qulonglong>(SourceCapability::Artwork)) == 0);

    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QCOMPARE(qobject_cast<IMusicSourceArtworkSession *>(session), nullptr);
}

void SourceManagerTest::legacyArtworkMetadataFallsBackToV1BaseSession()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.frozen-v1"),
                                QStringLiteral("account-1"),
                                QStringLiteral("Frozen v1 Account")};

    QVERIFY(harness.addPackage(frozenV1PluginDirectory(), QStringLiteral("frozen_v1"),
                               QStringLiteral("org.quemusic.source.frozen-v1"), account.sourceId,
                               QStringLiteral("Frozen v1 Source")));
    QCOMPARE(manager.loadAll(), 1);
    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QCOMPARE(qobject_cast<IMusicSourceArtworkSession *>(session), nullptr);

    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{account.sourceId, QStringLiteral("cover-1")};
    const QUuid requestId = manager.requestArtwork(account.sourceId, session, track);

    QVERIFY(!requestId.isNull());
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("fetchArtwork"));
    QCOMPARE(succeeded.constFirst().at(2).toString(), QStringLiteral("cover-1"));
}

void SourceManagerTest::optionalArtworkInterfaceWithoutMetadataIsNotUsed()
{
    SourceManagerHarness harness(false);
    SourceManager &manager = harness.manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.optional-without-metadata"),
                                QStringLiteral("account-1"),
                                QStringLiteral("No Artwork Metadata Account")};

    QVERIFY(harness.addPackage(fixtureDirectory(QStringLiteral("optional-without-metadata")),
                               QStringLiteral("optional_artwork_without_metadata"),
                               QStringLiteral("org.quemusic.source.optional-without-metadata"),
                               account.sourceId, QStringLiteral("Optional Artwork Without Metadata")));
    QCOMPARE(manager.loadAll(), 1);
    const QVariantMap descriptor = manager.availableSources().constFirst().toMap();
    QVERIFY((descriptor.value(QStringLiteral("capabilities")).toULongLong() &
             static_cast<qulonglong>(SourceCapability::Artwork)) == 0);

    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QVERIFY(qobject_cast<IMusicSourceArtworkSession *>(session) != nullptr);
    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    QSignalSpy failed(session, &IMusicSourceSession::requestFailed);

    const QUuid requestId = manager.requestArtwork(
        account.sourceId, session,
        {account.sourceId, QStringLiteral("must-not-dispatch")});

    QVERIFY(requestId.isNull());
    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void SourceManagerTest::sourceSessionKeepsNativePackageLoaded()
{
    PluginManager plugins;
    plugins.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    SourceManager manager(&plugins);
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    QCOMPARE(manager.loadAll(), 1);
    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QCOMPARE(plugins.unload(QStringLiteral("org.quemusic.source.fixture")),
             PluginOperationResult::Busy);

    delete session;
    QCOMPARE(plugins.unload(QStringLiteral("org.quemusic.source.fixture")),
             PluginOperationResult::Success);
}

void SourceManagerTest::nativePackageLifecycleKeepsSourceRegistryInSync()
{
    PluginManager plugins;
    plugins.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    SourceManager manager(&plugins);
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};
    const QString packageId = QStringLiteral("org.quemusic.source.fixture");

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({account.sourceId}));

    QCOMPARE(plugins.unload(packageId), PluginOperationResult::Success);
    QCOMPARE(manager.sourceIds(), QStringList());
    QVERIFY(manager.createSession(account.sourceId, account, &parent) == nullptr);

    QCOMPARE(plugins.reload(packageId), PluginOperationResult::Success);
    QCOMPARE(manager.sourceIds(), QStringList({account.sourceId}));
    QVERIFY(manager.createSession(account.sourceId, account, &parent) != nullptr);
}

void SourceManagerTest::failedSourceInitializationMarksPackageFailed()
{
    QTemporaryDir packageRoot;
    PluginManager plugins;
    bool manifestWritten = false;
    const QString packagePath = initializationFailurePackage(&packageRoot, &manifestWritten);
    QVERIFY(manifestWritten);
    QVERIFY(!packagePath.isEmpty());
    plugins.addSearchPath(packagePath);
    SourceManager sources(&plugins);

    QCOMPARE(sources.loadAll(), 0);
    const PluginSpec package = plugins.plugin(
        QStringLiteral("org.quemusic.source.initialization-failure"));
    QCOMPARE(package.state, PluginState::Failed);
    QVERIFY(package.error.contains(QStringLiteral("initialization")));
}

void SourceManagerTest::forwardsPackageActivationFailureToSourceLoadFailed()
{
    QTemporaryDir packageRoot;
    PluginManager plugins;
    bool manifestWritten = false;
    const QString packagePath = initializationFailurePackage(&packageRoot, &manifestWritten);
    QVERIFY(manifestWritten);
    QVERIFY(!packagePath.isEmpty());
    plugins.addSearchPath(packagePath);
    SourceManager sources(&plugins);
    QSignalSpy failures(&sources, &SourceManager::sourceLoadFailed);

    QCOMPARE(sources.loadAll(), 0);
    QCOMPARE(failures.count(), 1);
    const QList<QVariant> failure = failures.constFirst();
    QVERIFY(!failure.at(0).toString().isEmpty());
    QVERIFY(!failure.at(1).toString().isEmpty());
    QVERIFY(failure.at(1).toString().contains(QStringLiteral("initialization")));
}

void SourceManagerTest::failedSourceActivationLeavesPackageFailed()
{
    QTemporaryDir packageRoot;
    PluginManager plugins;
    bool manifestWritten = false;
    const QString packagePath = initializationFailurePackage(&packageRoot, &manifestWritten);
    QVERIFY(manifestWritten);
    QVERIFY(!packagePath.isEmpty());
    plugins.addSearchPath(packagePath);
    SourceManager sources(&plugins);
    QCOMPARE(plugins.discover(), 1);

    QVERIFY(!sources.loadSourcePackage(
        QStringLiteral("org.quemusic.source.initialization-failure")));
    QCOMPARE(plugins.plugin(QStringLiteral("org.quemusic.source.initialization-failure")).state,
             PluginState::Failed);
    QVERIFY(!sources.loadSourcePackage(
        QStringLiteral("org.quemusic.source.initialization-failure")));
}

void SourceManagerTest::destroyedPluginManagerRemovesPackageSources()
{
    auto *plugins = new PluginManager;
    SourceManager sources(plugins);
    plugins->addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    QCOMPARE(sources.loadAll(), 1);
    delete plugins;

    QCOMPARE(sources.sourceIds(), QStringList());
    QVERIFY(sources.createSession(QStringLiteral("test-source"), {}, nullptr) == nullptr);
}

void SourceManagerTest::managerlessSourceManagerCannotLoadNativePlugins()
{
    SourceManager sources;
    sources.addSearchPath(testSourcePluginDirectory());

    QCOMPARE(sources.loadAll(), 0);
    QCOMPARE(sources.sourceIds(), QStringList());
}

void SourceManagerTest::pluginManagerLoadReportsSourceActivationFailure()
{
    QTemporaryDir packageRoot;
    PluginManager plugins;
    bool manifestWritten = false;
    const QString packagePath = initializationFailurePackage(&packageRoot, &manifestWritten);
    QVERIFY(manifestWritten);
    QVERIFY(!packagePath.isEmpty());
    plugins.addSearchPath(packagePath);
    SourceManager sources(&plugins);
    const QString packageId = QStringLiteral("org.quemusic.source.initialization-failure");

    QCOMPARE(plugins.discover(), 1);
    QVERIFY(!plugins.load(packageId));
    QCOMPARE(plugins.plugin(packageId).state, PluginState::Failed);
}

QTEST_MAIN(SourceManagerTest)
#include "tst_SourceManager.moc"
