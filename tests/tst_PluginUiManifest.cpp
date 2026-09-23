#include "PluginManifest.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {
QJsonObject baseManifest()
{
    return {{QStringLiteral("id"), QStringLiteral("org.example.source")},
            {QStringLiteral("sourceId"), QStringLiteral("example")},
            {QStringLiteral("name"), QStringLiteral("Example")},
            {QStringLiteral("version"), QStringLiteral("1.0")},
            {QStringLiteral("category"), QStringLiteral("source")},
            {QStringLiteral("runtime"), QStringLiteral("native-qt")},
            {QStringLiteral("library"), QStringLiteral("libexample.dylib")},
            {QStringLiteral("pluginApi"), QJsonObject{{QStringLiteral("major"), 1},
                                                       {QStringLiteral("minHostMinor"), 0}}},
            {QStringLiteral("interfaces"), QJsonArray{QJsonObject{
                {QStringLiteral("id"), QStringLiteral("org.quemusic.MusicSourcePlugin/2.0")},
                {QStringLiteral("version"), QStringLiteral("2.0")}}}},
            {QStringLiteral("runtimeRequirements"), QJsonObject{
                {QStringLiteral("sourceSdkAbi"), 2},
                {QStringLiteral("qtMajor"), QT_VERSION_MAJOR},
                {QStringLiteral("architecture"), QStringLiteral("x86_64")},
                {QStringLiteral("buildKey"), QStringLiteral("Debug")}}}};
}

void writePackage(const QString &root, const QJsonObject &manifest)
{
    QFile library(QDir(root).filePath(QStringLiteral("libexample.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();
    QVERIFY(QDir(root).mkpath(QStringLiteral("qml")));
    QFile page(QDir(root).filePath(QStringLiteral("qml/ManagementPage.qml")));
    QVERIFY(page.open(QIODevice::WriteOnly));
    page.write("import QtQuick\nItem {}\n");
    page.close();
    QFile file(QDir(root).filePath(QStringLiteral("manifest.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(manifest).toJson());
}

PluginManifest parse(const QString &root, QString *error)
{
    return PluginManifest::fromFile(QDir(root).filePath(QStringLiteral("manifest.json")), error);
}
}

class PluginUiManifestTest final : public QObject {
    Q_OBJECT
private slots:
    void acceptsSchemaOnlyPluginWithoutUi();
    void acceptsPluginUiV1Declaration();
    void rejectsInvalidPluginUiDeclaration_data();
    void rejectsInvalidPluginUiDeclaration();
};

void PluginUiManifestTest::acceptsSchemaOnlyPluginWithoutUi()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writePackage(dir.path(), baseManifest());
    QString error;
    const auto manifest = parse(dir.path(), &error);
    QVERIFY2(manifest.isValid(), qPrintable(error));
    QVERIFY(!manifest.hasManagementUi());
    QVERIFY(manifest.pluginUiApiVersion().isEmpty());
    QVERIFY(manifest.managementUiRelativePath().isEmpty());
}

void PluginUiManifestTest::acceptsPluginUiV1Declaration()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto object = baseManifest();
    object.insert(QStringLiteral("pluginUiApi"), QStringLiteral("1.0"));
    object.insert(QStringLiteral("ui"), QJsonObject{
        {QStringLiteral("management"), QStringLiteral("qml/ManagementPage.qml")}});
    writePackage(dir.path(), object);
    QString error;
    const auto manifest = parse(dir.path(), &error);
    QVERIFY2(manifest.isValid(), qPrintable(error));
    QVERIFY(manifest.hasManagementUi());
    QCOMPARE(manifest.pluginUiApiVersion(), QStringLiteral("1.0"));
    QCOMPARE(manifest.managementUiRelativePath(), QStringLiteral("qml/ManagementPage.qml"));
}

void PluginUiManifestTest::rejectsInvalidPluginUiDeclaration_data()
{
    QTest::addColumn<QJsonValue>("api");
    QTest::addColumn<QJsonValue>("ui");
    QTest::addColumn<QString>("error");
    const auto page = [](const QString &path) {
        return QJsonValue(QJsonObject{{QStringLiteral("management"), path}});
    };
    const QJsonValue goodApi(QStringLiteral("1.0"));
    const QJsonValue goodPage = page(QStringLiteral("qml/ManagementPage.qml"));
    QTest::newRow("api-missing-with-ui") << QJsonValue(QJsonValue::Undefined) << goodPage
        << QStringLiteral("Manifest management UI requires pluginUiApi");
    QTest::newRow("api-number") << QJsonValue(1) << goodPage
        << QStringLiteral("Manifest plugin UI API must be a string");
    QTest::newRow("api-unsupported") << QJsonValue(QStringLiteral("2.0")) << goodPage
        << QStringLiteral("Manifest plugin UI API is unsupported");
    QTest::newRow("ui-missing-with-api") << goodApi << QJsonValue(QJsonValue::Undefined)
        << QStringLiteral("Manifest plugin UI management path is required");
    QTest::newRow("ui-array") << goodApi << QJsonValue(QJsonArray{})
        << QStringLiteral("Manifest UI must be an object");
    QTest::newRow("management-empty") << goodApi << page(QString())
        << QStringLiteral("Manifest plugin UI management path is invalid");
    QTest::newRow("management-absolute") << goodApi << page(QStringLiteral("/tmp/Page.qml"))
        << QStringLiteral("Manifest plugin UI management path must be package-relative");
    QTest::newRow("management-parent") << goodApi << page(QStringLiteral("qml/../Page.qml"))
        << QStringLiteral("Manifest plugin UI management path escapes package directory");
    QTest::newRow("management-encoded-parent") << goodApi
        << page(QStringLiteral("qml/%2e%2e/Page.qml"))
        << QStringLiteral("Manifest plugin UI management path escapes package directory");
    QTest::newRow("management-dot") << goodApi << page(QStringLiteral("qml/./Page.qml"))
        << QStringLiteral("Manifest plugin UI management path escapes package directory");
    QTest::newRow("management-remote") << goodApi
        << page(QStringLiteral("https://example.test/Page.qml"))
        << QStringLiteral("Manifest plugin UI management path must be package-relative");
    QTest::newRow("management-missing-file") << goodApi
        << page(QStringLiteral("qml/Absent.qml"))
        << QStringLiteral("Manifest plugin UI management file does not exist");
}

void PluginUiManifestTest::rejectsInvalidPluginUiDeclaration()
{
    QFETCH(QJsonValue, api);
    QFETCH(QJsonValue, ui);
    QFETCH(QString, error);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto object = baseManifest();
    if (!api.isUndefined()) object.insert(QStringLiteral("pluginUiApi"), api);
    if (!ui.isUndefined()) object.insert(QStringLiteral("ui"), ui);
    writePackage(dir.path(), object);
    QString actual;
    const auto manifest = parse(dir.path(), &actual);
    QVERIFY(!manifest.isValid());
    QCOMPARE(actual, error);
}

QTEST_MAIN(PluginUiManifestTest)
#include "tst_PluginUiManifest.moc"
