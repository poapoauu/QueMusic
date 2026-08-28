#include "SourceStartup.h"

#include "SourceManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

QStringList defaultSourcePluginSearchPaths(const QCoreApplication &application)
{
    return {
        QDir(application.applicationDirPath()).filePath(QStringLiteral("plugins/source")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("plugins/source")),
    };
}

SourceManager *createAndLoadSourceManager(const QCoreApplication &application, QObject *parent)
{
    auto *manager = new SourceManager(parent);
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
