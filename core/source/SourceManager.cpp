#include "SourceManager.h"

#include "IMusicSourceArtworkSession.h"
#include "PluginManager.h"

#include <QVariantMap>

#include <algorithm>

namespace {

class SourceSessionLeaseGuard final : public QObject {
public:
    explicit SourceSessionLeaseGuard(PluginLease lease, QObject *parent)
        : QObject(parent)
        , m_lease(std::move(lease))
    {
    }

private:
    PluginLease m_lease;
};

}

SourceManager::SourceManager(PluginManager *pluginManager, QObject *parent)
    : QObject(parent)
    , m_pluginManager(pluginManager)
{
    if (m_pluginManager != nullptr) {
        connect(m_pluginManager, &PluginManager::pluginChanged, this,
                [this](const QString &packageId) { synchronizeSourcePackage(packageId); });
        connect(m_pluginManager, &PluginManager::pluginLoadFailed, this,
                [this](const QString &packageId, const QString &error) {
                    QString origin = packageId;
                    if (m_pluginManager != nullptr) {
                        const PluginSpec package = m_pluginManager->plugin(packageId);
                        if (!package.path.isEmpty()) {
                            origin = package.path;
                        }
                    }
                    emit sourceLoadFailed(origin, error);
                });
        connect(m_pluginManager, &QObject::destroyed, this, [this] {
            m_pluginManager = nullptr;
            const auto firstRemoved = std::remove_if(
                m_sources.begin(), m_sources.end(), [](const LoadedSource &source) {
                    return !source.packageId.isEmpty();
                });
            if (firstRemoved == m_sources.end()) {
                return;
            }
            m_sources.erase(firstRemoved, m_sources.end());
            emit sourceChanged();
        });
    }
}

SourceManager::~SourceManager() = default;

void SourceManager::addSearchPath(const QString &path)
{
    if (m_pluginManager != nullptr) {
        m_pluginManager->addSearchPath(path);
    }
}

int SourceManager::loadAll()
{
    if (m_pluginManager == nullptr) {
        return 0;
    }

    int loadedCount = 0;
    m_pluginManager->discover();
    for (const QVariant &value : m_pluginManager->plugins()) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("category")).toString() == QStringLiteral("source")
            && loadSourcePackage(plugin.value(QStringLiteral("id")).toString())) {
            ++loadedCount;
        }
    }
    return loadedCount;
}

bool SourceManager::loadSourcePackage(const QString &packageId)
{
    if (m_pluginManager == nullptr) {
        return false;
    }

    if (m_pluginManager->plugin(packageId).state != PluginState::Loaded
        && !m_pluginManager->load(packageId)) {
        return false;
    }

    if (std::any_of(m_sources.cbegin(), m_sources.cend(),
                    [&packageId](const LoadedSource &source) {
                        return source.packageId == packageId;
                    })) {
        return true;
    }

    const PluginSpec package = m_pluginManager->plugin(packageId);
    if (package.category != PluginCategory::Source) {
        m_pluginManager->failLoadedPlugin(packageId,
                                          QStringLiteral("Package category is not source"));
        return false;
    }
    QObject *pluginObject = m_pluginManager->pluginInstance(packageId);
    IMusicSourcePlugin *plugin = qobject_cast<IMusicSourcePlugin *>(pluginObject);
    if (plugin == nullptr) {
        m_pluginManager->failLoadedPlugin(packageId,
                                          QStringLiteral("Package does not implement IMusicSourcePlugin"));
        return false;
    }

    const SourceDescriptor descriptor = plugin->descriptor();
    if (descriptor.id != package.sourceId) {
        m_pluginManager->failLoadedPlugin(
            packageId, QStringLiteral("Plugin source descriptor ID does not match manifest source ID"));
        return false;
    }
    if (descriptor.name.isEmpty()) {
        m_pluginManager->failLoadedPlugin(packageId, QStringLiteral("Plugin display name is empty"));
        return false;
    }
    if (!isMusicSourceSdkVersionCompatible(descriptor.sdkVersion)) {
        m_pluginManager->failLoadedPlugin(
            packageId, QStringLiteral("Unsupported plugin SDK version: %1 (host supports %2)")
                           .arg(descriptor.sdkVersion,
                                QStringLiteral(QUEMUSIC_MUSIC_SOURCE_SDK_VERSION)));
        return false;
    }
    if (std::any_of(m_sources.cbegin(), m_sources.cend(),
                    [&descriptor](const LoadedSource &source) {
                        return source.descriptor.id == descriptor.id;
                    })) {
        m_pluginManager->failLoadedPlugin(
            packageId, QStringLiteral("Duplicate plugin source ID: %1").arg(descriptor.id));
        return false;
    }

    SourcePluginContext context{&m_network, {}};
    if (!plugin->initialize(context)) {
        m_pluginManager->failLoadedPlugin(packageId, QStringLiteral("Plugin initialization failed"));
        return false;
    }

    m_sources.push_back({descriptor, plugin, packageId});
    emit sourceLoaded(descriptor.id);
    emit sourceChanged();
    return true;
}

void SourceManager::synchronizeSourcePackage(const QString &packageId)
{
    if (m_pluginManager == nullptr) {
        return;
    }

    const PluginSpec package = m_pluginManager->plugin(packageId);
    if (package.category != PluginCategory::Source) {
        return;
    }

    if (package.state == PluginState::Loaded) {
        loadSourcePackage(packageId);
        return;
    }

    removeSourcePackage(packageId);
}

void SourceManager::removeSourcePackage(const QString &packageId)
{
    const auto firstRemoved = std::remove_if(m_sources.begin(), m_sources.end(),
                                             [&packageId](const LoadedSource &source) {
                                                 return source.packageId == packageId;
                                             });
    if (firstRemoved == m_sources.end()) {
        return;
    }

    m_sources.erase(firstRemoved, m_sources.end());
    emit sourceChanged();
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

    PluginLease lease;
    if (!source->packageId.isEmpty()) {
        if (m_pluginManager == nullptr) {
            return nullptr;
        }
        lease = m_pluginManager->acquire(source->packageId);
        if (!lease.isValid()) {
            return nullptr;
        }
    }

    IMusicSourceSession *session = source->plugin->createSession(account, parent);
    if (session != nullptr && lease.isValid()) {
        new SourceSessionLeaseGuard(std::move(lease), session);
    }
    return session;
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

PluginManager *SourceManager::pluginManager() const
{
    return m_pluginManager;
}
