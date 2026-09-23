#pragma once

#include <QString>
#include <QUrl>

#define QUEMUSIC_PLUGIN_UI_API_V1 "1.0"

enum class PluginUiMode { Create, Edit };

struct PluginUiContextData {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    PluginUiMode mode = PluginUiMode::Create;
};

struct ManagementUiDescriptor {
    QString uiApiVersion;
    QUrl componentUrl;
    bool supportsCreate = false;
    bool supportsEdit = false;
};
