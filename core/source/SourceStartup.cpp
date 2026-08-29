#include "SourceStartup.h"

#include "SourceManager.h"
#include "PluginManager.h"

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

SourceManager *createAndLoadSourceManager(const QCoreApplication &application, QObject *parent)
{
    auto *plugins = new PluginManager(parent);
    auto *manager = new SourceManager(plugins, parent);
    const QStringList searchPaths = defaultSourcePluginSearchPaths(application);

    for (const QString &searchPath : searchPaths) {
        manager->addSearchPath(searchPath);
    }

    QObject::connect(manager, &SourceManager::sourceLoadFailed, manager,
                     [](const QString &pluginPath, const QString &error) {
                         qWarning().noquote()
                             << QStringLiteral("SourceManager plugin load failed: %1 (%2)")
                                    .arg(pluginPath, error);
                     });
    manager->loadAll();
    return manager;
}

SourceManager *initializeSourceStartupBoundary(const QCoreApplication &application,
                                              QQmlApplicationEngine &engine)
{
    SourceManager *manager =
        createAndLoadSourceManager(application, const_cast<QCoreApplication *>(&application));
    engine.rootContext()->setContextProperty(QStringLiteral("pluginManager"),
                                             manager->pluginManager());
    return manager;
}
