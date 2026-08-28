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

    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{QStringLiteral("test-source"), QStringLiteral("test-track-1")};
    const QUuid requestId = session->fetchArtwork(track);

    QVERIFY(succeeded.wait(1000));
    const QList<QVariant> arguments = succeeded.constFirst();
    QCOMPARE(arguments.at(0).toUuid(), requestId);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("fetchArtwork"));
    const QJsonObject artwork = arguments.at(2).toJsonValue().toObject();
    QCOMPARE(artwork.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.invalid/test-track-1.png"));
    QCOMPARE(artwork.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
}

QTEST_MAIN(SourceManagerTest)
#include "tst_SourceManager.moc"
