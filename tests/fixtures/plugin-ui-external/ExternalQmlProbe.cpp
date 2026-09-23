#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDebug>
#include <QScopedPointer>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[1])
                         + QStringLiteral("/share/quemusic/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        PluginPage { title: "Installed module probe" }
    )", QUrl());
    if (!component.isReady()) {
        qWarning().noquote() << component.errorString();
        return 3;
    }
    QScopedPointer<QObject> page(component.create());
    return page ? 0 : 4;
}
