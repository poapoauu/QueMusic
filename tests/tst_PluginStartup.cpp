#include "SourceManager.h"
#include "SourceStartup.h"

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
};

void PluginStartupTest::startupAddsApplicationAndUserPluginDirectories()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("BroNekoX"));
    QCoreApplication::setApplicationName(QStringLiteral("QueMusic"));

    const QStringList searchPaths = defaultSourcePluginSearchPaths(*QCoreApplication::instance());
    QCOMPARE(searchPaths.size(), 2);
    QCOMPARE(searchPaths.at(0),
             QDir(QCoreApplication::applicationDirPath())
                 .filePath(QStringLiteral("plugins/source")));
    QCOMPARE(searchPaths.at(1),
             QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                 .filePath(QStringLiteral("plugins/source")));
}

void PluginStartupTest::startupBoundaryKeepsEngineUsableWithoutRawQmlExposure()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("BroNekoX"));
    QCoreApplication::setApplicationName(QStringLiteral("QueMusic"));

    QQmlApplicationEngine engine;
    SourceManager *manager =
        initializeSourceStartupBoundary(*QCoreApplication::instance(), engine);
    QVERIFY(manager != nullptr);
    QCOMPARE(manager->parent(), QCoreApplication::instance());
    const QStringList searchPaths = defaultSourcePluginSearchPaths(*QCoreApplication::instance());
    QCOMPARE(searchPaths.size(), 2);
    QVERIFY(!QDir(searchPaths.at(1)).exists());

    engine.rootContext()->setContextProperty(QStringLiteral("startupSentinel"),
                                             QStringLiteral("still-usable"));
    QCOMPARE(engine.rootContext()->contextProperty(QStringLiteral("startupSentinel")).toString(),
             QStringLiteral("still-usable"));
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceManager")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceSession")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("musicSourcePlugin")).isValid());
}

QTEST_MAIN(PluginStartupTest)
#include "tst_PluginStartup.moc"
