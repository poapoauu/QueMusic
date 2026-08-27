#pragma once

#include "IPlaybackEngine.h"
#include "IPlaybackPlugin.h"

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <vector>

class PlaybackEngineManager : public QObject {
public:
    explicit PlaybackEngineManager(QObject *parent = nullptr);

    // The plugin must also be a QObject. The caller owns the plugin; this
    // registration is removed automatically when that QObject is destroyed.
    bool registerPlugin(IPlaybackPlugin *plugin);
    QStringList engineIds() const;

    bool useEngine(const QString &engineId);
    IPlaybackEngine *currentEngine() const;

private:
    struct RegisteredPlugin {
        QString id;
        QPointer<QObject> object;
        IPlaybackPlugin *plugin = nullptr;
    };

    std::vector<RegisteredPlugin> m_plugins;
    IPlaybackEngine *m_currentEngine = nullptr;
};
