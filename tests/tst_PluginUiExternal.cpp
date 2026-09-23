#include "PluginManager.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"

#include <QGuiApplication>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3) return 2;
    PluginManager manager;
    manager.addSearchPath(QString::fromLocal8Bit(argv[1]));
    if (manager.discover() != 1) return 3;
    const QString id = QStringLiteral("org.quemusic.source.external-ui-fixture");
    if (!manager.load(id)) {
        qCritical() << manager.plugin(id).error;
        return 4;
    }
    auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager.pluginInstance(id));
    if (!provider || provider->managementUi().componentUrl
                         != QUrl(QStringLiteral("qml/ManagementPage.qml"))) {
        return 5;
    }
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[2])
                         + QStringLiteral("/share/quemusic/qml"));
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])
                                                + QStringLiteral("/package/qml/ManagementPage.qml")));
    if (!component.isReady()) {
        qCritical() << component.errorString();
        return 6;
    }
    QScopedPointer<QObject> page(component.create());
    if (!page) {
        qCritical() << component.errorString();
        return 7;
    }
    return 0;
}
