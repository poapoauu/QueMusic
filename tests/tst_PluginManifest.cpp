#include "PluginManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {

constexpr auto sourceV1InterfaceId = "org.quemusic.MusicSourcePlugin/1.0";
constexpr auto sourceV2InterfaceId = "org.quemusic.MusicSourcePlugin/2.0";

QJsonObject validNativeSourceManifest()
{
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("org.quemusic.source.fixture")},
        {QStringLiteral("sourceId"), QStringLiteral("fixture")},
        {QStringLiteral("name"), QStringLiteral("Fixture")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("category"), QStringLiteral("source")},
        {QStringLiteral("runtime"), QStringLiteral("native-qt")},
        {QStringLiteral("library"), QStringLiteral("libfixture.dylib")},
        {QStringLiteral("pluginApi"), QJsonObject{{QStringLiteral("major"), 1},
                                                   {QStringLiteral("minHostMinor"), 0}}},
        {QStringLiteral("interfaces"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QString::fromLatin1(sourceV1InterfaceId)},
                        {QStringLiteral("version"), QStringLiteral("1.0")}}}},
    };
}

QJsonObject validV2NativeSourceManifest()
{
    QJsonObject manifest = validNativeSourceManifest();
    manifest.insert(QStringLiteral("id"), QStringLiteral("org.quemusic.source.fixture-v2"));
    manifest.insert(QStringLiteral("sourceId"), QStringLiteral("fixture-v2"));
    manifest.insert(QStringLiteral("name"), QStringLiteral("Fixture V2"));
    manifest.insert(QStringLiteral("version"), QStringLiteral("2.0.0"));
    manifest.insert(
        QStringLiteral("interfaces"),
        QJsonArray{QJsonObject{{QStringLiteral("id"), QString::fromLatin1(sourceV2InterfaceId)},
                               {QStringLiteral("version"), QStringLiteral("2.0")}}});
    manifest.insert(QStringLiteral("runtimeRequirements"),
                    QJsonObject{{QStringLiteral("sourceSdkAbi"), 2},
                                {QStringLiteral("qtMajor"), QT_VERSION_MAJOR},
                                {QStringLiteral("architecture"), QStringLiteral("x86_64")},
                                {QStringLiteral("buildKey"), QStringLiteral("Debug")}});
    return manifest;
}

QJsonObject manifestWithLibrary(const QString &library)
{
    QJsonObject manifest = validNativeSourceManifest();
    manifest.insert(QStringLiteral("library"), library);
    return manifest;
}

void writeManifest(const QString &directoryPath, const QJsonObject &manifest)
{
    QFile file(QDir(directoryPath).filePath(QStringLiteral("manifest.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

}

class PluginManifestTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsNativeSourcePackage();
    void acceptsV2SourceInterface();
    void rejectsSourceInterfaceAbiMismatch_data();
    void rejectsSourceInterfaceAbiMismatch();
    void rejectsAmbiguousSourceInterfaceDeclarations_data();
    void rejectsAmbiguousSourceInterfaceDeclarations();
    void rejectsIncompleteV2RuntimeRequirements_data();
    void rejectsIncompleteV2RuntimeRequirements();
    void normalizesSupportedArchitectures_data();
    void normalizesSupportedArchitectures();
    void rejectsUnknownArchitecture();
    void rejectsMalformedPluginApi_data();
    void rejectsMalformedPluginApi();
    void rejectsMalformedRuntimeRequirements_data();
    void rejectsMalformedRuntimeRequirements();
    void rejectsLibraryOutsidePackage();
    void rejectsLibraryDirectoryPath();
    void rejectsLibrarySymlinkOutsidePackage();
};

void PluginManifestTest::rejectsMalformedPluginApi_data()
{
    QTest::addColumn<QJsonValue>("pluginApi");

    QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("null") << QJsonValue(QJsonValue::Null);
    QTest::newRow("array") << QJsonValue(QJsonArray{});
    QTest::newRow("major-missing")
        << QJsonValue(QJsonObject{{QStringLiteral("minHostMinor"), 0}});
    QTest::newRow("major-string")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), QStringLiteral("1")},
                                  {QStringLiteral("minHostMinor"), 0}});
    QTest::newRow("major-fractional")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), 1.5},
                                  {QStringLiteral("minHostMinor"), 0}});
    QTest::newRow("major-unsupported")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), 2},
                                  {QStringLiteral("minHostMinor"), 0}});
    QTest::newRow("minimum-minor-missing")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), 1}});
    QTest::newRow("minimum-minor-string")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), 1},
                                  {QStringLiteral("minHostMinor"), QStringLiteral("0")}});
    QTest::newRow("minimum-minor-fractional")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), 1},
                                  {QStringLiteral("minHostMinor"), 0.5}});
    QTest::newRow("minimum-minor-negative")
        << QJsonValue(QJsonObject{{QStringLiteral("major"), 1},
                                  {QStringLiteral("minHostMinor"), -1}});
}

void PluginManifestTest::rejectsMalformedPluginApi()
{
    QFETCH(QJsonValue, pluginApi);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validNativeSourceManifest();
    if (pluginApi.isUndefined()) {
        manifestObject.remove(QStringLiteral("pluginApi"));
    } else {
        manifestObject.insert(QStringLiteral("pluginApi"), pluginApi);
    }
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY2(!manifest.isValid(), qPrintable(error));
    QVERIFY(!error.isEmpty());
}

void PluginManifestTest::rejectsMalformedRuntimeRequirements_data()
{
    QTest::addColumn<QJsonValue>("runtimeRequirements");

    QTest::newRow("null") << QJsonValue(QJsonValue::Null);
    QTest::newRow("array") << QJsonValue(QJsonArray{});
    QTest::newRow("string") << QJsonValue(QStringLiteral("native"));
    QTest::newRow("qt-major-string")
        << QJsonValue(QJsonObject{{QStringLiteral("qtMajor"), QStringLiteral("6")}});
    QTest::newRow("qt-major-fractional")
        << QJsonValue(QJsonObject{{QStringLiteral("qtMajor"), 6.5}});
    QTest::newRow("qt-major-negative")
        << QJsonValue(QJsonObject{{QStringLiteral("qtMajor"), -1}});
    QTest::newRow("qt-major-zero")
        << QJsonValue(QJsonObject{{QStringLiteral("qtMajor"), 0}});
    QTest::newRow("architecture-number")
        << QJsonValue(QJsonObject{{QStringLiteral("architecture"), 64}});
    QTest::newRow("architecture-empty")
        << QJsonValue(QJsonObject{{QStringLiteral("architecture"), QStringLiteral("")}});
    QTest::newRow("build-key-boolean")
        << QJsonValue(QJsonObject{{QStringLiteral("buildKey"), true}});
    QTest::newRow("build-key-empty")
        << QJsonValue(QJsonObject{{QStringLiteral("buildKey"), QStringLiteral("")}});
    QTest::newRow("source-sdk-abi-string")
        << QJsonValue(QJsonObject{{QStringLiteral("sourceSdkAbi"), QStringLiteral("1")}});
}

void PluginManifestTest::rejectsMalformedRuntimeRequirements()
{
    QFETCH(QJsonValue, runtimeRequirements);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validNativeSourceManifest();
    manifestObject.insert(QStringLiteral("runtimeRequirements"), runtimeRequirements);
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY2(!manifest.isValid(), qPrintable(error));
    QVERIFY(!error.isEmpty());
}

void PluginManifestTest::acceptsNativeSourcePackage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();
    writeManifest(directory.path(), validNativeSourceManifest());

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY2(manifest.isValid(), qPrintable(error));
    QCOMPARE(manifest.id(), QStringLiteral("org.quemusic.source.fixture"));
    QCOMPARE(manifest.category(), PluginCategory::Source);
    QCOMPARE(manifest.sourceSdkAbi(), 1);
    QCOMPARE(manifest.sourceInterfaceId(), QString::fromLatin1(sourceV1InterfaceId));
    QCOMPARE(manifest.libraryAbsolutePath(),
             QFileInfo(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")))
                 .canonicalFilePath());
}

void PluginManifestTest::acceptsV2SourceInterface()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();
    writeManifest(directory.path(), validV2NativeSourceManifest());

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY2(manifest.isValid(), qPrintable(error));
    QCOMPARE(manifest.sourceSdkAbi(), 2);
    QCOMPARE(manifest.sourceInterfaceId(), QString::fromLatin1(sourceV2InterfaceId));
    QCOMPARE(manifest.requiredQtMajor(), QT_VERSION_MAJOR);
    QCOMPARE(manifest.requiredArchitecture(), QStringLiteral("x86_64"));
    QCOMPARE(manifest.requiredBuildKey(), QStringLiteral("Debug"));
}

void PluginManifestTest::rejectsSourceInterfaceAbiMismatch_data()
{
    QTest::addColumn<QString>("interfaceId");
    QTest::addColumn<QJsonValue>("sourceSdkAbi");
    QTest::addColumn<QString>("errorFragment");

    QTest::newRow("v2-interface-with-v1-abi")
        << QString::fromLatin1(sourceV2InterfaceId) << QJsonValue(1)
        << QStringLiteral("does not match");
    QTest::newRow("v1-interface-with-v2-abi")
        << QString::fromLatin1(sourceV1InterfaceId) << QJsonValue(2)
        << QStringLiteral("does not match");
    QTest::newRow("unknown-abi")
        << QString::fromLatin1(sourceV2InterfaceId) << QJsonValue(3)
        << QStringLiteral("unsupported");
    QTest::newRow("unknown-interface")
        << QStringLiteral("org.quemusic.MusicSourcePlugin/9.0") << QJsonValue(2)
        << QStringLiteral("source plugin interface");
    QTest::newRow("v2-interface-with-missing-abi")
        << QString::fromLatin1(sourceV2InterfaceId) << QJsonValue(QJsonValue::Undefined)
        << QStringLiteral("source SDK ABI");
}

void PluginManifestTest::rejectsSourceInterfaceAbiMismatch()
{
    QFETCH(QString, interfaceId);
    QFETCH(QJsonValue, sourceSdkAbi);
    QFETCH(QString, errorFragment);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validV2NativeSourceManifest();
    manifestObject.insert(
        QStringLiteral("interfaces"),
        QJsonArray{QJsonObject{{QStringLiteral("id"), interfaceId},
                               {QStringLiteral("version"),
                                interfaceId.endsWith(QStringLiteral("/1.0"))
                                    ? QStringLiteral("1.0") : QStringLiteral("2.0")}}});
    QJsonObject requirements = manifestObject.value(QStringLiteral("runtimeRequirements")).toObject();
    if (sourceSdkAbi.isUndefined()) {
        requirements.remove(QStringLiteral("sourceSdkAbi"));
    } else {
        requirements.insert(QStringLiteral("sourceSdkAbi"), sourceSdkAbi);
    }
    manifestObject.insert(QStringLiteral("runtimeRequirements"), requirements);
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(manifest.isValid() == false);
    QVERIFY2(error.contains(errorFragment), qPrintable(error));
}

void PluginManifestTest::rejectsAmbiguousSourceInterfaceDeclarations_data()
{
    QTest::addColumn<QJsonValue>("interfaces");
    QTest::addColumn<QString>("errorFragment");

    const QJsonObject v2{{QStringLiteral("id"), QString::fromLatin1(sourceV2InterfaceId)},
                         {QStringLiteral("version"), QStringLiteral("2.0")}};
    const QJsonObject v1{{QStringLiteral("id"), QString::fromLatin1(sourceV1InterfaceId)},
                         {QStringLiteral("version"), QStringLiteral("1.0")}};
    const QJsonObject unknown{
        {QStringLiteral("id"), QStringLiteral("org.quemusic.MusicSourcePlugin/9.0")},
        {QStringLiteral("version"), QStringLiteral("9.0")}};

    QTest::newRow("known-plus-unknown")
        << QJsonValue(QJsonArray{v2, unknown}) << QStringLiteral("unsupported");
    QTest::newRow("duplicate-v2")
        << QJsonValue(QJsonArray{v2, v2}) << QStringLiteral("multiple");
    QTest::newRow("v1-plus-v2")
        << QJsonValue(QJsonArray{v1, v2}) << QStringLiteral("multiple");
}

void PluginManifestTest::rejectsAmbiguousSourceInterfaceDeclarations()
{
    QFETCH(QJsonValue, interfaces);
    QFETCH(QString, errorFragment);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validV2NativeSourceManifest();
    manifestObject.insert(QStringLiteral("interfaces"), interfaces);
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(manifest.isValid() == false);
    QVERIFY2(error.contains(errorFragment, Qt::CaseInsensitive), qPrintable(error));
}

void PluginManifestTest::rejectsIncompleteV2RuntimeRequirements_data()
{
    QTest::addColumn<QString>("missingField");
    QTest::newRow("runtime-requirements") << QStringLiteral("runtimeRequirements");
    QTest::newRow("qt-major") << QStringLiteral("qtMajor");
    QTest::newRow("architecture") << QStringLiteral("architecture");
    QTest::newRow("build-key") << QStringLiteral("buildKey");
}

void PluginManifestTest::rejectsIncompleteV2RuntimeRequirements()
{
    QFETCH(QString, missingField);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validV2NativeSourceManifest();
    if (missingField == QStringLiteral("runtimeRequirements")) {
        manifestObject.remove(missingField);
    } else {
        QJsonObject requirements =
            manifestObject.value(QStringLiteral("runtimeRequirements")).toObject();
        requirements.remove(missingField);
        manifestObject.insert(QStringLiteral("runtimeRequirements"), requirements);
    }
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(manifest.isValid() == false);
    QVERIFY2(error.contains(missingField == QStringLiteral("runtimeRequirements")
                                ? QStringLiteral("runtime requirements") : missingField,
                            Qt::CaseInsensitive),
             qPrintable(error));
}

void PluginManifestTest::normalizesSupportedArchitectures_data()
{
    QTest::addColumn<QString>("declaredArchitecture");
    QTest::addColumn<QString>("canonicalArchitecture");

    QTest::newRow("amd64-alias") << QStringLiteral("AMD64") << QStringLiteral("x86_64");
    QTest::newRow("x86-64-canonical")
        << QStringLiteral("x86_64") << QStringLiteral("x86_64");
    QTest::newRow("aarch64-alias") << QStringLiteral("aarch64") << QStringLiteral("arm64");
    QTest::newRow("arm64-canonical") << QStringLiteral("arm64") << QStringLiteral("arm64");
    QTest::newRow("universal") << QStringLiteral("universal") << QStringLiteral("universal");
}

void PluginManifestTest::normalizesSupportedArchitectures()
{
    QFETCH(QString, declaredArchitecture);
    QFETCH(QString, canonicalArchitecture);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validV2NativeSourceManifest();
    QJsonObject requirements =
        manifestObject.value(QStringLiteral("runtimeRequirements")).toObject();
    requirements.insert(QStringLiteral("architecture"), declaredArchitecture);
    manifestObject.insert(QStringLiteral("runtimeRequirements"), requirements);
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY2(manifest.isValid(), qPrintable(error));
    QCOMPARE(manifest.requiredArchitecture(), canonicalArchitecture);
}

void PluginManifestTest::rejectsUnknownArchitecture()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile library(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")));
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.close();

    QJsonObject manifestObject = validV2NativeSourceManifest();
    QJsonObject requirements =
        manifestObject.value(QStringLiteral("runtimeRequirements")).toObject();
    requirements.insert(QStringLiteral("architecture"), QStringLiteral("mips64-unknown"));
    manifestObject.insert(QStringLiteral("runtimeRequirements"), requirements);
    writeManifest(directory.path(), manifestObject);

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(manifest.isValid() == false);
    QVERIFY2(error.contains(QStringLiteral("architecture"), Qt::CaseInsensitive),
             qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("unsupported"), Qt::CaseInsensitive),
             qPrintable(error));
}

void PluginManifestTest::rejectsLibraryOutsidePackage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeManifest(directory.path(), manifestWithLibrary(QStringLiteral("../outside.dylib")));

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(!manifest.isValid());
    QVERIFY(error.contains(QStringLiteral("library")));
}

void PluginManifestTest::rejectsLibraryDirectoryPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeManifest(directory.path(), manifestWithLibrary(QStringLiteral(".")));

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(!manifest.isValid());
    QVERIFY(error.contains(QStringLiteral("library")));
}

void PluginManifestTest::rejectsLibrarySymlinkOutsidePackage()
{
    QTemporaryDir package;
    QTemporaryDir outside;
    const QString external = QDir(outside.path()).filePath(QStringLiteral("external.dylib"));
    QFile externalFile(external);
    QVERIFY(externalFile.open(QIODevice::WriteOnly));
    externalFile.close();
    QVERIFY(QFile::link(external, QDir(package.path()).filePath(QStringLiteral("escape.dylib"))));
    writeManifest(package.path(), manifestWithLibrary(QStringLiteral("escape.dylib")));

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(package.path()).filePath(QStringLiteral("manifest.json")), &error);

    QVERIFY(!manifest.isValid());
    QVERIFY(error.contains(QStringLiteral("package")));
}

QTEST_MAIN(PluginManifestTest)
#include "tst_PluginManifest.moc"
