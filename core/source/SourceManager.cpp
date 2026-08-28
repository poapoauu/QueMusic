#include "SourceManager.h"

#include "IMusicSourceArtworkSession.h"

#include <QDir>
#include <QDirIterator>
#include <QLibrary>
#include <QPluginLoader>
#include <QVariantMap>

#include <algorithm>

SourceManager::SourceManager(QObject *parent)
    : QObject(parent)
{
}

SourceManager::~SourceManager() = default;

void SourceManager::addSearchPath(const QString &path)
{
    m_searchPaths.append(path);
}

int SourceManager::loadAll()
{
    int loadedCount = 0;
    for (const QString &searchPath : m_searchPaths) {
        if (!QDir(searchPath).exists()) {
            reportLoadFailure(searchPath, QStringLiteral("Plugin search path does not exist"));
            continue;
        }

        QDirIterator iterator(searchPath, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString pluginPath = iterator.next();
            if (!QLibrary::isLibrary(pluginPath)) {
                continue;
            }

            auto loader = std::make_unique<QPluginLoader>(pluginPath);
            loader->setLoadHints(QLibrary::PreventUnloadHint);
            QObject *pluginObject = loader->instance();
            if (pluginObject == nullptr) {
                reportLoadFailure(pluginPath, loader->errorString());
                continue;
            }

            IMusicSourcePlugin *plugin = qobject_cast<IMusicSourcePlugin *>(pluginObject);
            if (plugin == nullptr) {
                reportLoadFailure(pluginPath, QStringLiteral("Plugin does not implement IMusicSourcePlugin"));
                continue;
            }

            const SourceDescriptor descriptor = plugin->descriptor();
            if (descriptor.id.isEmpty()) {
                reportLoadFailure(pluginPath, QStringLiteral("Plugin source ID is empty"));
                continue;
            }
            if (std::any_of(m_sources.cbegin(), m_sources.cend(),
                            [&descriptor](const LoadedSource &source) {
                                return source.descriptor.id == descriptor.id;
                            })) {
                reportLoadFailure(pluginPath, QStringLiteral("Duplicate plugin source ID: %1").arg(descriptor.id));
                continue;
            }
            if (!isMusicSourceSdkVersionCompatible(descriptor.sdkVersion)) {
                reportLoadFailure(pluginPath,
                                  QStringLiteral("Unsupported plugin SDK version: %1 (host supports %2)")
                                      .arg(descriptor.sdkVersion,
                                           QStringLiteral(QUEMUSIC_MUSIC_SOURCE_SDK_VERSION)));
                continue;
            }
            if (descriptor.name.isEmpty()) {
                reportLoadFailure(pluginPath, QStringLiteral("Plugin display name is empty"));
                continue;
            }

            SourcePluginContext context{&m_network, {}};
            if (!plugin->initialize(context)) {
                reportLoadFailure(pluginPath, QStringLiteral("Plugin initialization failed"));
                continue;
            }

            m_sources.push_back({descriptor, plugin});
            m_loaders.push_back(std::move(loader));
            ++loadedCount;
            emit sourceLoaded(descriptor.id);
            emit sourceChanged();
        }
    }

    return loadedCount;
}

QVariantList SourceManager::availableSources() const
{
    QVariantList sources;
    for (const LoadedSource &source : m_sources) {
        const SourceDescriptor &descriptor = source.descriptor;
        sources.append(QVariantMap{
            {QStringLiteral("id"), descriptor.id},
            {QStringLiteral("name"), descriptor.name},
            {QStringLiteral("version"), descriptor.version},
            {QStringLiteral("protocol"), descriptor.protocol},
            {QStringLiteral("capabilities"),
             QVariant::fromValue(static_cast<qulonglong>(descriptor.capabilities.toInt()))},
        });
    }
    return sources;
}

QStringList SourceManager::sourceIds() const
{
    QStringList ids;
    for (const LoadedSource &source : m_sources) {
        ids.append(source.descriptor.id);
    }
    return ids;
}

IMusicSourceSession *SourceManager::createSession(const QString &sourceId,
                                                   const SourceAccount &account, QObject *parent)
{
    const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(),
                                     [&sourceId](const LoadedSource &candidate) {
                                         return candidate.descriptor.id == sourceId;
                                     });
    if (source == m_sources.cend()) {
        return nullptr;
    }

    return source->plugin->createSession(account, parent);
}

QUuid SourceManager::requestArtwork(const QString &sourceId, IMusicSourceSession *session,
                                    const TrackRef &track) const
{
    if (session == nullptr) {
        return {};
    }

    const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(),
                                     [&sourceId](const LoadedSource &candidate) {
                                         return candidate.descriptor.id == sourceId;
                                     });
    if (source == m_sources.cend()
        || !source->descriptor.capabilities.testFlag(SourceCapability::Artwork)) {
        return {};
    }

    if (auto *artworkSession = qobject_cast<IMusicSourceArtworkSession *>(session)) {
        return artworkSession->fetchArtwork(track);
    }

    // MusicSourcePlugin/1.0 retains this legacy base-session slot. It is the
    // compatibility path for v1 plugins that predate the optional interface.
    return session->fetchArtwork(track);
}

void SourceManager::reportLoadFailure(const QString &pluginPath, const QString &error)
{
    emit sourceLoadFailed(pluginPath, error);
}
