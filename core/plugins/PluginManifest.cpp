#include "PluginManifest.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <cmath>
#include <limits>

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
    return !components.contains(QStringLiteral("."))
        && !components.contains(QStringLiteral(".."));
}

bool jsonIntegerAtLeast(const QJsonValue &value, int minimum, int *result)
{
    if (!value.isDouble()) {
        return false;
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < minimum
        || number > std::numeric_limits<int>::max()) {
        return false;
    }

    *result = static_cast<int>(number);
    return true;
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
    const QString name = object.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        return invalidManifest(QStringLiteral("Manifest name is empty"), error);
    }
    const QString version = object.value(QStringLiteral("version")).toString();
    if (version.isEmpty()) {
        return invalidManifest(QStringLiteral("Manifest version is empty"), error);
    }
    const QString sourceId = object.value(QStringLiteral("sourceId")).toString();
    if (sourceId.isEmpty()) {
        return invalidManifest(QStringLiteral("Manifest source ID is empty"), error);
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
    if (!pluginApiValue.isObject()) {
        return invalidManifest(QStringLiteral("Manifest plugin API must be an object"), error);
    }
    const QJsonObject pluginApi = pluginApiValue.toObject();
    int pluginApiMajor = 0;
    if (!jsonIntegerAtLeast(pluginApi.value(QStringLiteral("major")), 0, &pluginApiMajor)
        || pluginApiMajor != hostPluginApiMajor) {
        return invalidManifest(QStringLiteral("Manifest plugin API major is unsupported"), error);
    }
    int minimumHostMinor = 0;
    if (!jsonIntegerAtLeast(pluginApi.value(QStringLiteral("minHostMinor")), 0,
                            &minimumHostMinor)) {
        return invalidManifest(QStringLiteral("Manifest plugin API minimum host minor is invalid"),
                               error);
    }
    if (!hasSourceInterface(object.value(QStringLiteral("interfaces")))) {
        return invalidManifest(QStringLiteral("Manifest is missing the source plugin interface"), error);
    }

    const QFileInfo packageRootInfo(QFileInfo(manifestPath).absolutePath());
    const QFileInfo libraryInfo(
        QDir(packageRootInfo.absoluteFilePath()).filePath(library));
    const QString packageRoot = packageRootInfo.canonicalFilePath();
    const QString libraryPath = libraryInfo.canonicalFilePath();
    if (packageRoot.isEmpty() || libraryPath.isEmpty() || !libraryInfo.isFile()
        || !libraryPath.startsWith(packageRoot + QDir::separator())) {
        return invalidManifest(QStringLiteral("Manifest library escapes package directory"), error);
    }

    PluginManifest manifest;
    manifest.m_valid = true;
    manifest.m_id = id;
    manifest.m_sourceId = sourceId;
    manifest.m_name = name;
    manifest.m_version = version;
    manifest.m_category = PluginCategory::Source;
    manifest.m_libraryAbsolutePath = libraryPath;
    manifest.m_minimumHostPluginApiMinor = minimumHostMinor;

    const QJsonValue runtimeRequirementsValue =
        object.value(QStringLiteral("runtimeRequirements"));
    if (!runtimeRequirementsValue.isUndefined()) {
        if (!runtimeRequirementsValue.isObject()) {
            return invalidManifest(QStringLiteral("Manifest runtime requirements must be an object"),
                                   error);
        }
        const QJsonObject runtimeRequirements = runtimeRequirementsValue.toObject();
        const QJsonValue qtMajor = runtimeRequirements.value(QStringLiteral("qtMajor"));
        if (!qtMajor.isUndefined()
            && !jsonIntegerAtLeast(qtMajor, 0, &manifest.m_requiredQtMajor)) {
            return invalidManifest(QStringLiteral("Manifest required Qt major is invalid"), error);
        }
        const QJsonValue architecture =
            runtimeRequirements.value(QStringLiteral("architecture"));
        if (!architecture.isUndefined() && !architecture.isString()) {
            return invalidManifest(QStringLiteral("Manifest required architecture is invalid"),
                                   error);
        }
        const QJsonValue buildMode = runtimeRequirements.value(QStringLiteral("buildMode"));
        if (!buildMode.isUndefined() && !buildMode.isString()) {
            return invalidManifest(QStringLiteral("Manifest required build mode is invalid"), error);
        }
        manifest.m_requiredArchitecture = architecture.toString();
        manifest.m_requiredBuildMode = buildMode.toString();
    }
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

QString PluginManifest::sourceId() const
{
    return m_sourceId;
}

QString PluginManifest::name() const
{
    return m_name;
}

QString PluginManifest::version() const
{
    return m_version;
}

PluginCategory PluginManifest::category() const
{
    return m_category;
}

QString PluginManifest::libraryAbsolutePath() const
{
    return m_libraryAbsolutePath;
}

int PluginManifest::minimumHostPluginApiMinor() const
{
    return m_minimumHostPluginApiMinor;
}

int PluginManifest::requiredQtMajor() const
{
    return m_requiredQtMajor;
}

QString PluginManifest::requiredArchitecture() const
{
    return m_requiredArchitecture;
}

QString PluginManifest::requiredBuildMode() const
{
    return m_requiredBuildMode;
}
