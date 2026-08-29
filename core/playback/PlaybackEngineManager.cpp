#include "PlaybackEngineManager.h"

#include <algorithm>

PlaybackEngineManager::PlaybackEngineManager(QObject *parent)
    : QObject(parent)
{
}

bool PlaybackEngineManager::registerPlugin(IPlaybackPlugin *plugin)
{
    if (plugin == nullptr) {
        return false;
    }

    QObject *pluginObject = dynamic_cast<QObject *>(plugin);
    if (pluginObject == nullptr) {
        return false;
    }

    const QString engineId = plugin->engineId();
    if (engineId.isEmpty()) {
        return false;
    }

    const auto duplicate = std::find_if(m_plugins.cbegin(), m_plugins.cend(),
                                        [&engineId](const RegisteredPlugin &candidate) {
                                            return !candidate.object.isNull()
                                                && candidate.id == engineId;
                                        });
    if (duplicate != m_plugins.cend()) {
        return false;
    }

    m_plugins.push_back({engineId, pluginObject, plugin});
    connect(pluginObject, &QObject::destroyed, this, [this](QObject *destroyedPlugin) {
        if (m_currentEnginePlugin == destroyedPlugin) {
            delete m_currentEngine;
            m_currentEngine = nullptr;
            m_currentEnginePlugin = nullptr;
        }
        m_plugins.erase(std::remove_if(m_plugins.begin(), m_plugins.end(),
                                       [destroyedPlugin](const RegisteredPlugin &candidate) {
                                           return candidate.object.isNull()
                                               || candidate.object.data() == destroyedPlugin;
                                       }),
                        m_plugins.end());
    });
    return true;
}

QStringList PlaybackEngineManager::engineIds() const
{
    QStringList ids;
    for (const RegisteredPlugin &plugin : m_plugins) {
        if (!plugin.object.isNull()) {
            ids.append(plugin.id);
        }
    }
    return ids;
}

bool PlaybackEngineManager::useEngine(const QString &engineId)
{
    const auto plugin = std::find_if(m_plugins.cbegin(), m_plugins.cend(),
                                     [&engineId](const RegisteredPlugin &candidate) {
                                         return !candidate.object.isNull()
                                             && candidate.id == engineId;
                                     });
    if (plugin == m_plugins.cend()) {
        return false;
    }

    IPlaybackEngine *engine = plugin->plugin->createEngine(this);
    if (engine == nullptr) {
        return false;
    }
    engine->setParent(this);

    delete m_currentEngine;
    m_currentEngine = engine;
    m_currentEnginePlugin = plugin->object.data();
    return true;
}

IPlaybackEngine *PlaybackEngineManager::currentEngine() const
{
    return m_currentEngine;
}
