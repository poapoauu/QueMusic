#pragma once

#include "IMusicSourceSessionV2.h"
#include "SourceV2Types.h"

#include <QObject>
#include <QtPlugin>

#define QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID "org.quemusic.MusicSourcePlugin/2.0"
#define QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI 2

class IMusicSourcePluginV2 {
public:
    virtual ~IMusicSourcePluginV2() = default;
    virtual SourceDescriptorV2 descriptor() const = 0;
    virtual IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                                 QObject *parent) = 0;
};

Q_DECLARE_INTERFACE(IMusicSourcePluginV2, QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
