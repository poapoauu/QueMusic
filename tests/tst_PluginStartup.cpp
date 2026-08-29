#include "SourceManager.h"
#include "SourceStartup.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class PluginStartupTest : public QObject {
    Q_OBJECT

private slots:
    void startupAddsApplicationAndUserPluginDirectories();
    void startupBoundaryKeepsEngineUsableWithoutRawQmlExposure();
    void startupLoadsBuiltNavidromePlugin();
    void sourceManagerCreatesNavidromeSessionAndDispatchesArtwork();
};

void PluginStartupTest::startupAddsApplicationAndUserPluginDirectories()
{
    QCoreApplication::setOrganizationName(QStringLiteral("BroNekoX"));
    QCoreApplication::setApplicationName(QStringLiteral("QueMusic"));

    const QStringList searchPaths = defaultSourcePluginSearchPaths(*QCoreApplication::instance());
    QCOMPARE(searchPaths.size(), 2);
    QCOMPARE(searchPaths.at(0),
             QDir(QCoreApplication::applicationDirPath())
                 .filePath(QStringLiteral("../plugins")));
    QCOMPARE(searchPaths.at(1),
             QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                 .filePath(QStringLiteral("plugins")));
}

void PluginStartupTest::startupBoundaryKeepsEngineUsableWithoutRawQmlExposure()
{
    QCoreApplication::setOrganizationName(QStringLiteral("BroNekoX"));
    QCoreApplication::setApplicationName(QStringLiteral("QueMusic"));

    QQmlApplicationEngine engine;
    SourceManager *manager =
        initializeSourceStartupBoundary(*QCoreApplication::instance(), engine);
    QVERIFY(manager != nullptr);
    QVERIFY(manager->pluginManager() != nullptr);
    QCOMPARE(manager->parent(), QCoreApplication::instance());
    const QStringList searchPaths = defaultSourcePluginSearchPaths(*QCoreApplication::instance());
    QCOMPARE(searchPaths.size(), 2);

    engine.rootContext()->setContextProperty(QStringLiteral("startupSentinel"),
                                             QStringLiteral("still-usable"));
    QCOMPARE(engine.rootContext()->contextProperty(QStringLiteral("startupSentinel")).toString(),
             QStringLiteral("still-usable"));
    QCOMPARE(engine.rootContext()
                 ->contextProperty(QStringLiteral("pluginManager"))
                 .value<QObject *>(),
             manager->pluginManager());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceManager")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceSession")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("musicSourcePlugin")).isValid());
}

void PluginStartupTest::startupLoadsBuiltNavidromePlugin()
{
    PluginManager plugins;
    SourceManager manager(&plugins);
    const QString pluginDirectory =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../plugins"));
    manager.addSearchPath(pluginDirectory);

    QVERIFY(manager.loadAll() >= 1);
    QVERIFY(manager.sourceIds().contains(QStringLiteral("navidrome")));
}

void PluginStartupTest::sourceManagerCreatesNavidromeSessionAndDispatchesArtwork()
{
    PluginManager plugins;
    SourceManager manager(&plugins);
    manager.addSearchPath(QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("../plugins")));
    QCOMPARE(manager.loadAll(), 1);
    const SourceAccount account{
        QStringLiteral("navidrome"),
        QStringLiteral("admin"),
        QStringLiteral("Navidrome Admin"),
        {{QStringLiteral("serverUrl"), QStringLiteral("http://example.invalid:8533")},
         {QStringLiteral("username"), QStringLiteral("admin")}},
        QByteArrayLiteral("test-password")};
    IMusicSourceSession *session = manager.createSession(QStringLiteral("navidrome"), account, &manager);
    QVERIFY(session != nullptr);
    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);

    const QUuid requestId = manager.requestArtwork(
        QStringLiteral("navidrome"), session,
        {QStringLiteral("navidrome"), QStringLiteral("cover-1")});

    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("fetchArtwork"));
}

QTEST_MAIN(PluginStartupTest)
#include "tst_PluginStartup.moc"
