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
constexpr int sourceSdkV1Abi = 1;
constexpr int sourceSdkV2Abi = 2;
constexpr auto sourceInterfacePrefix = "org.quemusic.MusicSourcePlugin/";
constexpr auto sourceV1InterfaceId = "org.quemusic.MusicSourcePlugin/1.0";
constexpr auto sourceV2InterfaceId = "org.quemusic.MusicSourcePlugin/2.0";

PluginManifest invalidManifest(const QString &message, QString *error)
{
    if (error) {
        *error = message;
    }
    return {};
}

QString parseSourceInterfaceId(const QJsonValue &interfaces, QString *error)
{
    if (!interfaces.isArray()) {
        *error = QStringLiteral("Manifest source plugin interfaces must be an array");
        return {};
    }

    QString recognizedInterface;
    for (const QJsonValue &interfaceValue : interfaces.toArray()) {
        if (!interfaceValue.isObject()) {
            continue;
        }
        const QString id = interfaceValue.toObject().value(QStringLiteral("id")).toString();
        if (!id.startsWith(QString::fromLatin1(sourceInterfacePrefix))) {
            continue;
        }
        if (id != QString::fromLatin1(sourceV1InterfaceId)
            && id != QString::fromLatin1(sourceV2InterfaceId)) {
            *error = QStringLiteral("Manifest source plugin interface %1 is unsupported").arg(id);
            return {};
        }
        if (!recognizedInterface.isEmpty()) {
            *error = QStringLiteral("Manifest declares multiple source plugin interfaces");
            return {};
        }
        recognizedInterface = id;
    }
    if (recognizedInterface.isEmpty()) {
        *error = QStringLiteral("Manifest is missing a supported source plugin interface");
    }
    return recognizedInterface;
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

QString canonicalPluginArchitecture(const QString &architecture)
{
    const QString value = architecture.trimmed().toLower();
    if (value == QStringLiteral("x86_64") || value == QStringLiteral("amd64")
        || value == QStringLiteral("x64")) {
        return QStringLiteral("x86_64");
    }
    if (value == QStringLiteral("arm64") || value == QStringLiteral("aarch64")) {
        return QStringLiteral("arm64");
    }
    if (value == QStringLiteral("universal") || value == QStringLiteral("universal2")) {
        return QStringLiteral("universal");
    }
    return {};
}

bool isPluginArchitectureCompatible(const QString &requiredArchitecture,
                                    const QString &hostArchitecture)
{
    const QString required = canonicalPluginArchitecture(requiredArchitecture);
    const QString host = canonicalPluginArchitecture(hostArchitecture);
    if (required == QStringLiteral("universal")) {
        return host == QStringLiteral("x86_64") || host == QStringLiteral("arm64");
    }
    return !required.isEmpty() && required == host;
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
    QString interfaceError;
    const QString sourceInterface =
        parseSourceInterfaceId(object.value(QStringLiteral("interfaces")), &interfaceError);
    if (sourceInterface.isEmpty()) {
        return invalidManifest(interfaceError, error);
    }

    const int interfaceAbi = sourceInterface == QString::fromLatin1(sourceV2InterfaceId)
        ? sourceSdkV2Abi : sourceSdkV1Abi;

    const QFileInfo packageRootInfo(QFileInfo(manifestPath).absolutePath());
    const QFileInfo libraryInfo(
        QDir(packageRootInfo.absoluteFilePath()).filePath(library));
    const QString packageRoot = packageRootInfo.canonicalFilePath();
    const QString libraryPath = libraryInfo.canonicalFilePath();
    if (packageRoot.isEmpty() || libraryPath.isEmpty() || !libraryInfo.isFile()
        || !libraryPath.startsWith(packageRoot + QDir::separator())) {
        return invalidManifest(QStringLiteral("Manifest library escapes package directory"), error);
    }

    const QJsonValue runtimeRequirementsValue =
        object.value(QStringLiteral("runtimeRequirements"));
    if (interfaceAbi == sourceSdkV2Abi && !runtimeRequirementsValue.isObject()) {
        return invalidManifest(QStringLiteral("Manifest v2 runtime requirements are required"),
                               error);
    }

    int sourceSdkAbi = sourceSdkV1Abi;
    int requiredQtMajor = 0;
    QString requiredArchitecture;
    QString requiredBuildKey;
    if (!runtimeRequirementsValue.isUndefined()) {
        if (!runtimeRequirementsValue.isObject()) {
            return invalidManifest(QStringLiteral("Manifest runtime requirements must be an object"),
                                   error);
        }
        const QJsonObject runtimeRequirements = runtimeRequirementsValue.toObject();

        const QJsonValue sourceSdkAbiValue =
            runtimeRequirements.value(QStringLiteral("sourceSdkAbi"));
        if (sourceSdkAbiValue.isUndefined()) {
            if (interfaceAbi == sourceSdkV2Abi) {
                return invalidManifest(QStringLiteral("Manifest v2 source SDK ABI is required"),
                                       error);
            }
        } else if (!jsonIntegerAtLeast(sourceSdkAbiValue, 1, &sourceSdkAbi)) {
            return invalidManifest(QStringLiteral("Manifest source SDK ABI is invalid"), error);
        }
        if (sourceSdkAbi != sourceSdkV1Abi && sourceSdkAbi != sourceSdkV2Abi) {
            return invalidManifest(QStringLiteral("Manifest source SDK ABI is unsupported"), error);
        }
        if (sourceSdkAbi != interfaceAbi) {
            return invalidManifest(
                QStringLiteral("Manifest source SDK ABI does not match source plugin interface"),
                error);
        }

        const QJsonValue qtMajor = runtimeRequirements.value(QStringLiteral("qtMajor"));
        if (interfaceAbi == sourceSdkV2Abi && qtMajor.isUndefined()) {
            return invalidManifest(QStringLiteral("Manifest v2 runtime requirement qtMajor is required"),
                                   error);
        }
        if (!qtMajor.isUndefined() && !jsonIntegerAtLeast(qtMajor, 1, &requiredQtMajor)) {
            return invalidManifest(QStringLiteral("Manifest required Qt major is invalid"), error);
        }
        const QJsonValue architecture =
            runtimeRequirements.value(QStringLiteral("architecture"));
        if (interfaceAbi == sourceSdkV2Abi && architecture.isUndefined()) {
            return invalidManifest(
                QStringLiteral("Manifest v2 runtime requirement architecture is required"), error);
        }
        if (!architecture.isUndefined()
            && (!architecture.isString() || architecture.toString().isEmpty())) {
            return invalidManifest(QStringLiteral("Manifest required architecture is invalid"),
                                   error);
        }
        QJsonValue buildKey = runtimeRequirements.value(QStringLiteral("buildKey"));
        if (interfaceAbi == sourceSdkV1Abi && buildKey.isUndefined()) {
            buildKey = runtimeRequirements.value(QStringLiteral("buildMode"));
        }
        if (interfaceAbi == sourceSdkV2Abi && buildKey.isUndefined()) {
            return invalidManifest(QStringLiteral("Manifest v2 runtime requirement buildKey is required"),
                                   error);
        }
        if (!buildKey.isUndefined()
            && (!buildKey.isString() || buildKey.toString().isEmpty())) {
            return invalidManifest(QStringLiteral("Manifest required build key is invalid"), error);
        }
        if (!architecture.isUndefined()) {
            requiredArchitecture = canonicalPluginArchitecture(architecture.toString());
            if (requiredArchitecture.isEmpty()) {
                return invalidManifest(
                    QStringLiteral("Manifest required architecture %1 is unsupported")
                        .arg(architecture.toString()),
                    error);
            }
        }
        requiredBuildKey = buildKey.toString();
    } else if (interfaceAbi != sourceSdkV1Abi) {
        return invalidManifest(QStringLiteral("Manifest source SDK ABI is missing"), error);
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
    manifest.m_sourceSdkAbi = sourceSdkAbi;
    manifest.m_sourceInterfaceId = sourceInterface;
    manifest.m_requiredQtMajor = requiredQtMajor;
    manifest.m_requiredArchitecture = requiredArchitecture;
    manifest.m_requiredBuildKey = requiredBuildKey;
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

int PluginManifest::sourceSdkAbi() const
{
    return m_sourceSdkAbi;
}

QString PluginManifest::sourceInterfaceId() const
{
    return m_sourceInterfaceId;
}

QString PluginManifest::requiredArchitecture() const
{
    return m_requiredArchitecture;
}

QString PluginManifest::requiredBuildKey() const
{
    return m_requiredBuildKey;
}
