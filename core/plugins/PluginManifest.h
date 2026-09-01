#pragma once

#include <QString>

enum class PluginCategory {
    Unknown,
    Source,
};

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
