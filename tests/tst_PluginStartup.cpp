#include "SourceStartup.h"
#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "SourceRegistry.h"
#include "MusicHub.h"
#include "PlaybackCoordinator.h"
#include "PluginSettingsController.h"
#include "SourceScopeStore.h"
#include "QtPlaybackController.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QCoreApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class PluginStartupTest : public QObject {
    Q_OBJECT

private slots:
    void startupAddsApplicationAndUserPluginDirectories();
    void startupBoundaryKeepsEngineUsableWithoutRawQmlExposure();
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

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    UnavailableSecretStore secrets;
    SourceAccountStore accounts(&settings, &secrets);
    auto plugins = createAndLoadPluginManager(*QCoreApplication::instance());
    QVERIFY(plugins);
    const QString navidromePackage = QStringLiteral("org.quemusic.source.navidrome");
    QCOMPARE(plugins->plugin(navidromePackage).state, PluginState::Loaded);
    QVERIFY(qobject_cast<IMusicSourcePluginV2 *>(
        plugins->pluginInstance(navidromePackage)));
    SourceRegistry registry(plugins.get(), &accounts);
    SourceScopeStore scope(&settings);
    QtPlaybackController playbackController;
    PlaybackCoordinator playback(&registry, &playbackController);
    playbackController.setCoordinator(&playback);
    MusicHub hub(&registry, &scope, &settings);
    PluginSettingsController pluginSettings(plugins.get(), &registry, &accounts);
    QQmlApplicationEngine engine;
    installSourceRuntimeContext(engine, plugins.get(), &hub, &playback,
                                &playbackController, &pluginSettings);
    const QStringList searchPaths = defaultSourcePluginSearchPaths(*QCoreApplication::instance());
    QCOMPARE(searchPaths.size(), 2);

    engine.rootContext()->setContextProperty(QStringLiteral("startupSentinel"),
                                             QStringLiteral("still-usable"));
    QCOMPARE(engine.rootContext()->contextProperty(QStringLiteral("startupSentinel")).toString(),
             QStringLiteral("still-usable"));
    QCOMPARE(engine.rootContext()
             ->contextProperty(QStringLiteral("pluginManager"))
                 .value<QObject *>(),
             plugins.get());
    QVERIFY(engine.rootContext()->contextProperty(QStringLiteral("musicHub"))
                .value<QObject *>());
    QVERIFY(engine.rootContext()->contextProperty(QStringLiteral("playbackCoordinator"))
                .value<QObject *>());
    QCOMPARE(engine.rootContext()->contextProperty(QStringLiteral("playbackController"))
                 .value<QObject *>(), static_cast<QObject *>(&playbackController));
    QVERIFY(engine.rootContext()->contextProperty(QStringLiteral("pluginSettings"))
                .value<QObject *>());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("mediaBridge")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceManager")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("sourceSession")).isValid());
    QVERIFY(!engine.rootContext()->contextProperty(QStringLiteral("musicSourcePlugin")).isValid());
}

QTEST_MAIN(PluginStartupTest)
#include "tst_PluginStartup.moc"
