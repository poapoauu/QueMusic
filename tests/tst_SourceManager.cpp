#include "IMusicSourceArtworkSession.h"
#include "PluginManager.h"
#include "SourceManager.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
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

QTEST_MAIN(SourceManagerTest)
#include "tst_SourceManager.moc"
