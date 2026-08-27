#pragma once

#include <QString>
#include <QtPlugin>

class IPlaybackEngine;
class QObject;

class IPlaybackPlugin {
public:
    virtual ~IPlaybackPlugin() = default;

    // Plugins registered with PlaybackEngineManager must also inherit QObject.
    // The manager observes that QObject lifetime and removes the registration
    // when its owner destroys the plugin.
    virtual QString engineId() const = 0;
    virtual QString engineName() const = 0;
    virtual IPlaybackEngine *createEngine(QObject *parent) = 0;
};

#define IPlaybackPlugin_iid "org.quemusic.IPlaybackPlugin/1.0"
Q_DECLARE_INTERFACE(IPlaybackPlugin, IPlaybackPlugin_iid)
