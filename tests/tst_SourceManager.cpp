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
    void failedSourceActivationCanBeRetriedWithoutSecondLoader();
    void destroyedPluginManagerRemovesPackageSources();
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

QString initializationFailurePackage(QTemporaryDir *root)
{
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
    manifest.write(QStringLiteral(R"({"id":"org.quemusic.source.initialization-failure","sourceId":"fixture.initialize-failure","name":"Initialization Failure Source","version":"1.0.0","category":"source","runtime":"native-qt","library":"%1","pluginApi":{"major":1,"minHostMinor":0},"interfaces":[{"id":"org.quemusic.MusicSourcePlugin/1.0","version":"1.0"}],"runtimeRequirements":{"qtMajor":6}})")
                       .arg(libraryName)
                       .toUtf8());
    manifest.close();
    return root->path();
}

}

void SourceManagerTest::loadsValidSourcePlugin()
{
    SourceManager manager;
    QSignalSpy loaded(&manager, &SourceManager::sourceLoaded);
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};
    QObject parent;

    manager.addSearchPath(testSourcePluginDirectory());

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
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);

    manager.addSearchPath(fixtureDirectory(QStringLiteral("does-not-exist")));

    QCOMPARE(manager.loadAll(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(manager.sourceIds(), QStringList());

    manager.addSearchPath(testSourcePluginDirectory());

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("test-source")}));
}

void SourceManagerTest::rejectsUnknownSourceId()
{
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("unknown"), QStringLiteral("account-1"),
                                QStringLiteral("Unknown Account")};

    manager.addSearchPath(testSourcePluginDirectory());
    QCOMPARE(manager.loadAll(), 1);

    QVERIFY(manager.createSession(account.sourceId, account, &parent) == nullptr);
}

void SourceManagerTest::rejectsDuplicateSourceIdWithoutStoppingOtherSources()
{
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.duplicate"), QStringLiteral("account-1"),
                                QStringLiteral("Duplicate Account")};

    manager.addSearchPath(fixtureDirectory(QStringLiteral("duplicate")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.duplicate")}));
    QVERIFY(manager.createSession(account.sourceId, account, &parent) != nullptr);
}

void SourceManagerTest::rejectsInvalidPluginsWithoutStoppingOtherSources()
{
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);

    manager.addSearchPath(fixtureDirectory(QStringLiteral("invalid")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(failed.count(), 5);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.valid")}));
}

void SourceManagerTest::rejectsUnsupportedSdkVersionWithoutStoppingOtherSources()
{
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);

    manager.addSearchPath(fixtureDirectory(QStringLiteral("incompatible")));
    manager.addSearchPath(testSourcePluginDirectory());

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("test-source")}));
    QVERIFY(failed.constFirst().at(1).toString().contains(QStringLiteral("2.0")));
}

void SourceManagerTest::fakeSourceReturnsNamespacedTrack()
{
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    manager.addSearchPath(testSourcePluginDirectory());

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
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    manager.addSearchPath(testSourcePluginDirectory());

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
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    manager.addSearchPath(testSourcePluginDirectory());
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
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("test-source"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    manager.addSearchPath(testSourcePluginDirectory());
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
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.valid"), QStringLiteral("account-1"),
                                QStringLiteral("Fixture Account")};

    manager.addSearchPath(fixtureDirectory(QStringLiteral("invalid")));
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
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.frozen-v1"),
                                QStringLiteral("account-1"),
                                QStringLiteral("Frozen v1 Account")};

    manager.addSearchPath(frozenV1PluginDirectory());
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
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.optional-without-metadata"),
                                QStringLiteral("account-1"),
                                QStringLiteral("No Artwork Metadata Account")};

    manager.addSearchPath(fixtureDirectory(QStringLiteral("optional-without-metadata")));
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
    const QString packagePath = initializationFailurePackage(&packageRoot);
    QVERIFY(!packagePath.isEmpty());
    plugins.addSearchPath(packagePath);
    SourceManager sources(&plugins);

    QCOMPARE(sources.loadAll(), 0);
    const PluginSpec package = plugins.plugin(
        QStringLiteral("org.quemusic.source.initialization-failure"));
    QCOMPARE(package.state, PluginState::Failed);
    QVERIFY(package.error.contains(QStringLiteral("initialization")));
}

void SourceManagerTest::failedSourceActivationCanBeRetriedWithoutSecondLoader()
{
    QTemporaryDir packageRoot;
    PluginManager plugins;
    const QString packagePath = initializationFailurePackage(&packageRoot);
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

QTEST_MAIN(SourceManagerTest)
#include "tst_SourceManager.moc"
