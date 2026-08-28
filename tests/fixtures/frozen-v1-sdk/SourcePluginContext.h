#pragma once

// Frozen from MusicSourcePlugin/1.0 as published on main at 8ad8e59.

#include <QString>

class QNetworkAccessManager;

struct SourcePluginContext {
    QNetworkAccessManager *network = nullptr;
    QString cacheRoot;
};
