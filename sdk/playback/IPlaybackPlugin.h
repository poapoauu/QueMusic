#pragma once

#include <QString>
#include <QtPlugin>

class IPlaybackEngine;
class QObject;

class IPlaybackPlugin {
public:
    virtual ~IPlaybackPlugin() = default;

    virtual QString engineId() const = 0;
    virtual QString engineName() const = 0;
    virtual IPlaybackEngine *createEngine(QObject *parent) = 0;
};

#define IPlaybackPlugin_iid "org.quemusic.IPlaybackPlugin/1.0"
Q_DECLARE_INTERFACE(IPlaybackPlugin, IPlaybackPlugin_iid)
