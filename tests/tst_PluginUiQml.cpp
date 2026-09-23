#include <QGuiApplication>
#include <QAccessible>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <QRegularExpression>
#include <QSignalSpy>
#include "PluginTheme.h"

class PluginUiQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void importsStandaloneModule();
    void themeIsReadOnly();
    void generalKitTypesInstantiate();
    void generalControlsBehavior();
    void keyboardNavigationAndAccessibility();
    void managementKitTypesInstantiate();
    void managementKitBehavior();
    void narrowLongTextLayoutHasNoWarnings();
};

void PluginUiQmlTest::importsStandaloneModule()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        PluginPage {
            width: 480
            title: "Fixture"
            property bool editModeValueIsStable: PluginUiMode.Edit === 1
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("title").toString(), QStringLiteral("Fixture"));
    QVERIFY(object->property("editModeValueIsStable").toBool());
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

void PluginUiQmlTest::generalKitTypesInstantiate()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    const QStringList types{
        QStringLiteral("PluginScrollPage"), QStringLiteral("PluginSection"),
        QStringLiteral("PluginGroup"), QStringLiteral("PluginLabel"),
        QStringLiteral("PluginDescription"), QStringLiteral("PluginSeparator"),
        QStringLiteral("PluginButton"), QStringLiteral("PluginPrimaryButton"),
        QStringLiteral("PluginDangerButton"), QStringLiteral("PluginIconButton"),
        QStringLiteral("PluginTextField"), QStringLiteral("PluginPasswordField"),
        QStringLiteral("PluginNumberField"), QStringLiteral("PluginUrlField"),
        QStringLiteral("PluginDirectoryField"), QStringLiteral("PluginSwitch"),
        QStringLiteral("PluginCheckBox"), QStringLiteral("PluginComboBox")};
    for (const QString &type : types) {
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\nimport QueMusic.PluginUI 1.0\n%1 {}")
                              .arg(type).toUtf8(), QUrl());
        QVERIFY2(component.isReady(), qPrintable(type + QStringLiteral(": ")
                                                + component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(type + QStringLiteral(": ")
                                   + component.errorString()));
    }
}

void PluginUiQmlTest::generalControlsBehavior()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        Item {
            property real themeScale: PluginTheme.scaleFactor
            PluginLabel { objectName: "label"; text: "Caption" }
            PluginButton { objectName: "button"; text: "Disabled"; enabled: false }
            PluginPasswordField { objectName: "password"; text: "secret" }
            PluginNumberField { objectName: "number"; from: 1; to: 10; value: 4 }
            PluginUrlField { objectName: "url"; text: "not-a-url" }
            PluginDirectoryField { objectName: "directory"; text: "/music" }
            PluginSwitch { objectName: "switch"; checked: true }
            PluginCheckBox { objectName: "checkbox"; checked: true }
            PluginComboBox { objectName: "combo"; model: ["one", "two"]; currentIndex: 1 }
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> form(component.create());
    QVERIFY2(form, qPrintable(component.errorString()));
    QCOMPARE(engine.singletonInstance<PluginTheme *>("QueMusic.PluginUI", "PluginTheme"),
             PluginTheme::instance());
    auto child = [&form](const char *name) { return form->findChild<QObject *>(name); };
    QVERIFY(child("button"));
    QVERIFY(!child("button")->property("enabled").toBool());
    QSignalSpy disabledClicked(child("button"), SIGNAL(clicked()));
    QVERIFY(QMetaObject::invokeMethod(child("button"), "click"));
    QCOMPARE(disabledClicked.count(), 0);
    QCOMPARE(child("password")->property("echoMode").toInt(), 2); // TextInput.Password
    QCOMPARE(child("number")->property("value").toDouble(), 4.0);
    QVERIFY(child("number")->setProperty("value", 100.0));
    QCOMPARE(child("number")->property("value").toDouble(), 10.0);
    QVERIFY(!child("url")->property("acceptableInput").toBool());
    QVERIFY(child("url")->setProperty("text", QStringLiteral("https://example.com/music")));
    QVERIFY(child("url")->property("acceptableInput").toBool());
    QCOMPARE(child("directory")->property("text").toString(), QStringLiteral("/music"));
    QSignalSpy browseRequested(child("directory"), SIGNAL(browseRequested()));
    auto *browseButton = child("directory")->findChild<QObject *>("pluginDirectoryBrowseButton");
    QVERIFY(browseButton);
    QVERIFY(QMetaObject::invokeMethod(browseButton, "click"));
    QCOMPARE(browseRequested.count(), 1);
    QVERIFY(child("switch")->property("checked").toBool());
    QVERIFY(child("checkbox")->property("checked").toBool());
    QCOMPARE(child("combo")->property("currentText").toString(), QStringLiteral("two"));

    const qreal originalHeight = child("button")->property("implicitHeight").toReal();
    const int originalFontSize = child("label")->property("font").value<QFont>().pixelSize();
    PluginThemeTokens tokens;
    tokens.fontCaption = PluginTheme::instance()->fontCaption();
    tokens.fontBody = PluginTheme::instance()->fontBody();
    tokens.fontTitle = PluginTheme::instance()->fontTitle();
    tokens.scaleFactor = 1.25;
    PluginTheme::instance()->apply(tokens);
    QCoreApplication::processEvents();
    QCOMPARE(PluginTheme::instance()->scaleFactor(), 1.25);
    QCOMPARE(form->property("themeScale").toReal(), 1.25);
    QVERIFY(child("button")->property("implicitHeight").toReal() > originalHeight);
    QVERIFY(child("label")->property("font").value<QFont>().pixelSize() > originalFontSize);
    tokens.scaleFactor = 1.0;
    PluginTheme::instance()->apply(tokens);
}

void PluginUiQmlTest::keyboardNavigationAndAccessibility()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Window
        import QueMusic.PluginUI 1.0
        Window {
            width: 320; height: 240; visible: true
            Column {
                PluginTextField { objectName: "first"; placeholderText: "Server address" }
                PluginButton { objectName: "disabled"; text: "Unavailable"; enabled: false }
                PluginButton { objectName: "second"; text: "Save account" }
                PluginSwitch { objectName: "third"; text: "Enable sync" }
            }
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.data());
    QVERIFY(window);
    auto child = [&object](const char *name) { return object->findChild<QQuickItem *>(name); };
    auto *first = child("first");
    auto *second = child("second");
    auto *third = child("third");
    QVERIFY(first && second && third);

    first->forceActiveFocus();
    QVERIFY(first->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY(second->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY(third->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Backtab);
    QVERIFY(second->hasActiveFocus());

    auto accessibleName = [](QObject *target) {
        QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(target);
        return interface ? interface->text(QAccessible::Name) : QString();
    };
    QCOMPARE(accessibleName(first), QStringLiteral("Server address"));
    QCOMPARE(accessibleName(second), QStringLiteral("Save account"));
    QCOMPARE(accessibleName(third), QStringLiteral("Enable sync"));
    QCOMPARE(warnings.count(), 0);
}

void PluginUiQmlTest::managementKitTypesInstantiate()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    const QStringList types{QStringLiteral("PluginStatus"),
                            QStringLiteral("PluginBadge"),
                            QStringLiteral("PluginBusyIndicator"),
                            QStringLiteral("PluginErrorState"),
                            QStringLiteral("PluginEmptyState"),
                            QStringLiteral("PluginAccountCard"),
                            QStringLiteral("PluginServerCard"),
                            QStringLiteral("PluginQrCode"),
                            QStringLiteral("PluginQrLogin")};
    for (const QString &type : types) {
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\nimport QueMusic.PluginUI 1.0\n%1 {}")
                              .arg(type).toUtf8(), QUrl());
        QVERIFY2(component.isReady(), qPrintable(type + QStringLiteral(": ")
                                                + component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(type + QStringLiteral(": ")
                                   + component.errorString()));
    }
}

void PluginUiQmlTest::managementKitBehavior()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        Item {
            PluginStatus { objectName: "status"; status: "error"; text: "Offline" }
            PluginBadge { objectName: "badge"; text: "" }
            PluginBusyIndicator { objectName: "busy"; running: false }
            PluginErrorState { objectName: "error"; actionVisible: true }
            PluginEmptyState { objectName: "empty"; actionVisible: true }
            PluginAccountCard { objectName: "account" }
            PluginServerCard { objectName: "server" }
            PluginQrCode { objectName: "qr"; source: "https://example.com/qr.png" }
            PluginQrCode { objectName: "relativeQr"; source: "qr.png" }
            PluginQrLogin { objectName: "login"; qrSource: "" }
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> form(component.create());
    QVERIFY2(form, qPrintable(component.errorString()));
    auto child = [&form](const char *name) { return form->findChild<QObject *>(name); };
    QCOMPARE(child("status")->property("statusColor").value<QColor>(),
             PluginTheme::instance()->danger());
    QCOMPARE(child("badge")->property("implicitWidth").toReal(), 0.0);
    QVERIFY(!child("busy")->property("animating").toBool());
    QCOMPARE(child("server")->property("status").toString(), QStringLiteral("warning"));
    QVERIFY(child("qr")->property("error").toBool());
    QVERIFY(child("relativeQr")->property("validSource").toBool());
    QVERIFY(child("login"));

    QSignalSpy retry(child("error"), SIGNAL(retryRequested()));
    QVERIFY(QMetaObject::invokeMethod(child("error")->findChild<QObject *>("pluginErrorAction"),
                                      "click"));
    QCOMPARE(retry.count(), 1);
    QSignalSpy emptyAction(child("empty"), SIGNAL(actionRequested()));
    QVERIFY(QMetaObject::invokeMethod(child("empty")->findChild<QObject *>("pluginEmptyAction"),
                                      "click"));
    QCOMPARE(emptyAction.count(), 1);
    QSignalSpy refresh(child("login"), SIGNAL(refreshRequested()));
    QSignalSpy cancel(child("login"), SIGNAL(cancelRequested()));
    QVERIFY(QMetaObject::invokeMethod(child("login")->findChild<QObject *>("pluginQrRefresh"),
                                      "click"));
    QVERIFY(QMetaObject::invokeMethod(child("login")->findChild<QObject *>("pluginQrCancel"),
                                      "click"));
    QCOMPARE(refresh.count(), 1);
    QCOMPARE(cancel.count(), 1);
}

void PluginUiQmlTest::narrowLongTextLayoutHasNoWarnings()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QUEMUSIC_QML_IMPORT_DIR));
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QueMusic.PluginUI 1.0
        Item {
            width: 120; height: 400
            PluginErrorState {
                id: errorState
                objectName: "errorState"
                width: parent.width
                title: "A very long connection error title that must wrap"
                description: "A detailed recovery message that must remain inside a narrow plugin panel."
            }
            property bool contentFits: errorState.children.every(
                function(child) { return !child.visible || child.width <= errorState.width })
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QVERIFY(object->property("contentFits").toBool());
    QCOMPARE(warnings.count(), 0);
}

QTEST_MAIN(PluginUiQmlTest)
#include "tst_PluginUiQml.moc"
