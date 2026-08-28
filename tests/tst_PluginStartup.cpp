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
    void startupRecipeLoadsWithoutQmlExposure();
    void startupCreatesApplicationLifetimeManager();
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

void PluginStartupTest::startupRecipeLoadsWithoutQmlExposure()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("BroNekoX"));
    QCoreApplication::setApplicationName(QStringLiteral("QueMusic"));

    QObject owner;
    const QStringList searchPaths = defaultSourcePluginSearchPaths(*QCoreApplication::instance());
    SourceManager manager(&owner);
    QSignalSpy failures(&manager, &SourceManager::sourceLoadFailed);

    for (const QString &searchPath : searchPaths) {
        manager.addSearchPath(searchPath);
    }

    const int loadedCount = manager.loadAll();
    QVERIFY(loadedCount >= 0);

    int missingPathCount = 0;
    for (const QString &searchPath : searchPaths) {
        if (!QDir(searchPath).exists()) {
            ++missingPathCount;
        }
    }
    QVERIFY(failures.count() >= missingPathCount);

    QQmlApplicationEngine engine;
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceManager")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceSession")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("musicSourcePlugin")).isValid());
}

void PluginStartupTest::startupCreatesApplicationLifetimeManager()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("BroNekoX"));
    QCoreApplication::setApplicationName(QStringLiteral("QueMusic"));

    QObject owner;
    SourceManager *manager = createAndLoadSourceManager(*QCoreApplication::instance(), &owner);
    QVERIFY(manager != nullptr);
    QCOMPARE(manager->parent(), &owner);
}

QTEST_MAIN(PluginStartupTest)
#include "tst_PluginStartup.moc"
