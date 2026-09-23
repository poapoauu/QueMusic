#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <QRegularExpression>

class PluginUiQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void importsStandaloneModule();
    void themeIsReadOnly();
};

void PluginUiQmlTest::importsStandaloneModule()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        PluginPage { width: 480; title: "Fixture" }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("title").toString(), QStringLiteral("Fixture"));
}

void PluginUiQmlTest::themeIsReadOnly()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        Item { Component.onCompleted: PluginTheme.primary = "red" }
    )", QUrl());
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral(".*Cannot assign to read-only property.*")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY(object);
}

QTEST_MAIN(PluginUiQmlTest)
#include "tst_PluginUiQml.moc"
