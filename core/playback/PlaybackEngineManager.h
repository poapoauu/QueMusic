#pragma once

#include "IPlaybackEngine.h"
#include "IPlaybackPlugin.h"

#include <QObject>
#include <QStringList>

#include <vector>

class PlaybackEngineManager : public QObject {
public:
    explicit PlaybackEngineManager(QObject *parent = nullptr);

    bool registerPlugin(IPlaybackPlugin *plugin);
    QStringList engineIds() const;

    bool useEngine(const QString &engineId);
    IPlaybackEngine *currentEngine() const;

private:
    std::vector<IPlaybackPlugin *> m_plugins;
    IPlaybackEngine *m_currentEngine = nullptr;
};
