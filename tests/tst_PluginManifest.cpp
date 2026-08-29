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
            QJsonObject{{QStringLiteral("id"), QStringLiteral("org.quemusic.MusicSourcePlugin/1.0")},
                        {QStringLiteral("version"), QStringLiteral("1.0")}}}},
    };
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
    void rejectsLibraryOutsidePackage();
    void rejectsLibraryDirectoryPath();
    void rejectsLibrarySymlinkOutsidePackage();
};

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
    QCOMPARE(manifest.libraryAbsolutePath(),
             QFileInfo(QDir(directory.path()).filePath(QStringLiteral("libfixture.dylib")))
                 .canonicalFilePath());
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
