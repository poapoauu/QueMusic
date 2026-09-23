#pragma once

#include "PluginUiTypes.h"

#include <QObject>
#include <QtPlugin>

#define QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID \
    "org.quemusic.SourceManagementUiProvider/1.0"

class ISourceManagementUiProvider {
public:
    virtual ~ISourceManagementUiProvider() = default;
    virtual ManagementUiDescriptor managementUi() const = 0;
    virtual QObject *createManagementBackend(const PluginUiContextData &context,
                                             QObject *parent) = 0;
};

Q_DECLARE_INTERFACE(ISourceManagementUiProvider,
                    QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID)
