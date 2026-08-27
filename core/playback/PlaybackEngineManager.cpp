#include "PlaybackEngineManager.h"

#include <algorithm>

PlaybackEngineManager::PlaybackEngineManager(QObject *parent)
    : QObject(parent)
{
}

bool PlaybackEngineManager::registerPlugin(IPlaybackPlugin *plugin)
{
    if (plugin == nullptr || plugin->engineId().isEmpty()) {
        return false;
    }

    const auto duplicate = std::find_if(m_plugins.cbegin(), m_plugins.cend(),
                                        [plugin](const IPlaybackPlugin *candidate) {
                                            return candidate->engineId() == plugin->engineId();
                                        });
    if (duplicate != m_plugins.cend()) {
        return false;
    }

    m_plugins.push_back(plugin);
    return true;
}

QStringList PlaybackEngineManager::engineIds() const
{
    QStringList ids;
    for (const IPlaybackPlugin *plugin : m_plugins) {
        ids.append(plugin->engineId());
    }
    return ids;
}

bool PlaybackEngineManager::useEngine(const QString &engineId)
{
    const auto plugin = std::find_if(m_plugins.cbegin(), m_plugins.cend(),
                                     [&engineId](const IPlaybackPlugin *candidate) {
                                         return candidate->engineId() == engineId;
                                     });
    if (plugin == m_plugins.cend()) {
        return false;
    }

    IPlaybackEngine *engine = (*plugin)->createEngine(this);
    if (engine == nullptr) {
        return false;
    }
    engine->setParent(this);

    delete m_currentEngine;
    m_currentEngine = engine;
    return true;
}

IPlaybackEngine *PlaybackEngineManager::currentEngine() const
{
    return m_currentEngine;
}
