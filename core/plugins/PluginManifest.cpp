#include "PluginManifest.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

namespace {

constexpr int hostPluginApiMajor = 1;
constexpr auto sourceInterfaceId = "org.quemusic.MusicSourcePlugin/1.0";

PluginManifest invalidManifest(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return {};
}

bool hasSourceInterface(const QJsonValue &interfaces)
{
    if (!interfaces.isArray()) {
        return false;
    }

    for (const QJsonValue &interfaceValue : interfaces.toArray()) {
        if (interfaceValue.isObject()
            && interfaceValue.toObject().value(QStringLiteral("id")).toString()
                   == QString::fromLatin1(sourceInterfaceId)) {
            return true;
        }
    }
    return false;
}

bool isPackageRelativeLibraryPath(const QString &library)
{
    if (library.isEmpty() || QFileInfo(library).isAbsolute()) {
        return false;
    }

    const QStringList components = library.split(QRegularExpression(QStringLiteral("[/\\\\]")),
                                                 Qt::KeepEmptyParts);
    return !components.contains(QStringLiteral(".."));
}

}

PluginManifest PluginManifest::fromFile(const QString &manifestPath, QString *error)
{
    if (error) {
        error->clear();
    }

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return invalidManifest(QStringLiteral("Unable to read manifest: %1").arg(file.errorString()),
                               error);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return invalidManifest(QStringLiteral("Invalid manifest JSON: %1").arg(parseError.errorString()),
                               error);
    }

    const QJsonObject object = document.object();
    const QString id = object.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        return invalidManifest(QStringLiteral("Manifest id is empty"), error);
    }
    if (object.value(QStringLiteral("runtime")).toString() != QStringLiteral("native-qt")) {
        return invalidManifest(QStringLiteral("Manifest runtime is not native-qt"), error);
    }
    if (object.value(QStringLiteral("category")).toString() != QStringLiteral("source")) {
        return invalidManifest(QStringLiteral("Manifest category is not source"), error);
    }

    const QString library = object.value(QStringLiteral("library")).toString();
    if (!isPackageRelativeLibraryPath(library)) {
        return invalidManifest(QStringLiteral("Manifest library must be a non-empty package-relative path"),
                               error);
    }

    const QJsonValue pluginApiValue = object.value(QStringLiteral("pluginApi"));
    const QJsonValue pluginApiMajor = pluginApiValue.isObject()
        ? pluginApiValue.toObject().value(QStringLiteral("major"))
        : QJsonValue();
    if (!pluginApiMajor.isDouble()
        || pluginApiMajor.toDouble(-1) != hostPluginApiMajor) {
        return invalidManifest(QStringLiteral("Manifest plugin API major is unsupported"), error);
    }
    if (!hasSourceInterface(object.value(QStringLiteral("interfaces")))) {
        return invalidManifest(QStringLiteral("Manifest is missing the source plugin interface"), error);
    }

    PluginManifest manifest;
    manifest.m_valid = true;
    manifest.m_id = id;
    manifest.m_category = PluginCategory::Source;
    manifest.m_libraryAbsolutePath = QDir(QFileInfo(manifestPath).absolutePath()).filePath(library);
    return manifest;
}

bool PluginManifest::isValid() const
{
    return m_valid;
}

QString PluginManifest::id() const
{
    return m_id;
}

PluginCategory PluginManifest::category() const
{
    return m_category;
}

QString PluginManifest::libraryAbsolutePath() const
{
    return m_libraryAbsolutePath;
}
