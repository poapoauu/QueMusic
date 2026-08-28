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
    PluginCategory category() const;
    QString libraryAbsolutePath() const;

private:
    bool m_valid = false;
    QString m_id;
    PluginCategory m_category = PluginCategory::Unknown;
    QString m_libraryAbsolutePath;
};
