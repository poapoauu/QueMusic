#include "PluginManager.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3) return 2;
    const QString root = QString::fromLocal8Bit(argv[1]);
    const QString mode = QString::fromLocal8Bit(argv[2]);
    PluginManager manager;
    manager.addSearchPath(root);
    const int discovered = manager.discover();
    const QString packageId = QStringLiteral("org.quemusic.source.local");
    const bool loaded = manager.load(packageId);
    const auto package = manager.plugin(packageId);
    if (mode == QStringLiteral("load")) {
        if (discovered == 1 && loaded && package.state == PluginState::Loaded) return 0;
        qCritical() << "Installed Local package failed to load" << discovered << package.error;
        return 1;
    }
    if (mode == QStringLiteral("reject-discovery")) {
        if (discovered == 0 && !loaded) return 0;
        qCritical() << "Incompatible Local package was unexpectedly discovered" << discovered << loaded;
        return 1;
    }
    if (mode == QStringLiteral("reject") || mode == QStringLiteral("missing-runtime")) {
        if (discovered == 1 && !loaded && package.state == PluginState::Failed
            && !package.error.isEmpty()
            && (mode != QStringLiteral("missing-runtime")
                || package.error.contains(QStringLiteral("quemusic_source_content_events")))) return 0;
        qCritical() << "Local package rejection did not retain a load error"
                    << discovered << loaded << int(package.state) << package.error;
        return 1;
    }
    return 2;
}
