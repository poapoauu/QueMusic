#pragma once

#include <QString>

class QNetworkAccessManager;

struct SourcePluginContext {
    QNetworkAccessManager *network = nullptr;
    QString cacheRoot;
};
