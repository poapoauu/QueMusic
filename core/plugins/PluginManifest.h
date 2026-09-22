#pragma once

#include <QString>

enum class PluginCategory {
    Unknown,
    Source,
};

// Native source packages use x86_64, arm64, or universal in manifests.
// A universal package contains both supported slices and is loadable by an
// x86_64 or arm64 process; a single-architecture package must match exactly.
QString canonicalPluginArchitecture(const QString &architecture);
bool isPluginArchitectureCompatible(const QString &requiredArchitecture,
                                    const QString &hostArchitecture);

class PluginManifest {
public:
    static PluginManifest fromFile(const QString &manifestPath, QString *error = nullptr);

    bool isValid() const;
    QString id() const;
    QString sourceId() const;
    QString name() const;
    QString version() const;
    PluginCategory category() const;
    QString libraryAbsolutePath() const;
    int minimumHostPluginApiMinor() const;
    int sourceSdkAbi() const;
    QString sourceInterfaceId() const;
    int requiredQtMajor() const;
    QString requiredArchitecture() const;
    QString requiredBuildKey() const;

private:
    bool m_valid = false;
    QString m_id;
    QString m_sourceId;
    QString m_name;
    QString m_version;
    PluginCategory m_category = PluginCategory::Unknown;
    QString m_libraryAbsolutePath;
    int m_minimumHostPluginApiMinor = 0;
    int m_sourceSdkAbi = 0;
    QString m_sourceInterfaceId;
    int m_requiredQtMajor = 0;
    QString m_requiredArchitecture;
    QString m_requiredBuildKey;
};
