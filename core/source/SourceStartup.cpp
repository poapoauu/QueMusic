#include "SourceStartup.h"

#include "PluginManager.h"
#include "MusicHub.h"
#include "PlaybackCoordinator.h"
#include "PluginSettingsController.h"
#include "QtPlaybackController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

QStringList defaultSourcePluginSearchPaths(const QCoreApplication &application)
{
    const QDir applicationDirectory(application.applicationDirPath());
    const QString bundledPlugins =
        applicationDirectory.filePath(QStringLiteral("../PlugIns/quemusic"));
    const QString developmentPlugins =
        applicationDirectory.filePath(QStringLiteral("../plugins"));
    const QString applicationPluginPath =
        QFileInfo::exists(bundledPlugins) ? bundledPlugins : developmentPlugins;

    return {
        applicationPluginPath,
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("plugins")),
    };
}

std::unique_ptr<PluginManager> createAndLoadPluginManager(
    const QCoreApplication &application, QObject *parent)
{
    auto plugins = std::make_unique<PluginManager>(parent);
    for (const QString &searchPath : defaultSourcePluginSearchPaths(application))
        plugins->addSearchPath(searchPath);
    QObject::connect(plugins.get(), &PluginManager::pluginLoadFailed, plugins.get(),
                     [](const QString &packageId, const QString &error) {
        qWarning().noquote() << QStringLiteral("Plugin load failed: %1 (%2)")
                                    .arg(packageId, error);
    });
    plugins->discover();
    for (const QVariant &entry : plugins->plugins()) {
        const QString packageId = entry.toMap().value(QStringLiteral("id")).toString();
        if (!packageId.isEmpty()) plugins->load(packageId);
    }
    return plugins;
}

void installSourceRuntimeContext(QQmlApplicationEngine &engine,
                                 PluginManager *plugins, MusicHub *musicHub,
                                 PlaybackCoordinator *playbackCoordinator,
                                 QtPlaybackController *playbackController,
                                 PluginSettingsController *pluginSettings)
{
    engine.rootContext()->setContextProperty(QStringLiteral("pluginManager"), plugins);
    engine.rootContext()->setContextProperty(QStringLiteral("musicHub"), musicHub);
    engine.rootContext()->setContextProperty(QStringLiteral("playbackCoordinator"),
                                             playbackCoordinator);
    engine.rootContext()->setContextProperty(QStringLiteral("playbackController"),
                                             playbackController);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginSettings"), pluginSettings);
}
