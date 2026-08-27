#pragma once

#include "IMusicSourceSession.h"
#include "SourcePluginContext.h"
#include "SourceTypes.h"

#include <QObject>
#include <QtPlugin>

class IMusicSourcePlugin {
public:
    virtual ~IMusicSourcePlugin() = default;

    virtual SourceDescriptor descriptor() const = 0;
    virtual bool initialize(SourcePluginContext &context) = 0;
    virtual IMusicSourceSession *createSession(const SourceAccount &account, QObject *parent) = 0;
};

#define QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID "org.quemusic.MusicSourcePlugin/1.0"
Q_DECLARE_INTERFACE(IMusicSourcePlugin, QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID)
