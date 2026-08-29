#include "PluginManager.h"

#include "PluginManifest.h"
#include "IMusicSourcePlugin.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QPointer>
#include <QSysInfo>

#include <algorithm>
#include <functional>

namespace {

constexpr int hostPluginApiMinor = 0;

QString pluginStateName(PluginState state)
{
    switch (state) {
    case PluginState::Discovered:
        return QStringLiteral("discovered");
    case PluginState::Loaded:
        return QStringLiteral("loaded");
    case PluginState::Failed:
        return QStringLiteral("failed");
    case PluginState::Unloaded:
        return QStringLiteral("unloaded");
    }
    return QStringLiteral("failed");
}

QString pluginCategoryName(PluginCategory category)
{
    switch (category) {
    case PluginCategory::Source:
        return QStringLiteral("source");
    case PluginCategory::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString hostBuildMode()
{
#ifdef QT_NO_DEBUG
    return QStringLiteral("Release");
#else
    return QStringLiteral("Debug");
#endif
}

}

struct PluginLease::State {
    std::function<void()> release;

    ~State()
    {
        if (release) {
            release();
        }
    }
};

struct PluginManager::Entry {
    PluginManifest manifest;
    PluginSpec spec;
    std::unique_ptr<QPluginLoader> loader;
    QObject *instance = nullptr;
};

PluginLease::PluginLease(std::shared_ptr<State> state)
    : m_state(std::move(state))
{
}

bool PluginLease::isValid() const
{
    return m_state != nullptr;
}

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    connect(this, &PluginManager::pluginChanged, this, &PluginManager::pluginsChanged);
}

PluginManager::~PluginManager() = default;

void PluginManager::addSearchPath(const QString &path)
{
    if (!path.isEmpty() && !m_searchPaths.contains(path)) {
        m_searchPaths.append(path);
    }
}

int PluginManager::discover()
{
    int count = 0;
    for (const QString &searchPath : m_searchPaths) {
        const QDir root(searchPath);
        if (!root.exists()) {
            continue;
        }

        const QStringList directories = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &directory : directories) {
            const QString manifestPath = root.filePath(directory + QStringLiteral("/manifest.json"));
            if (!QFileInfo::exists(manifestPath)) {
                continue;
            }

            QString error;
            const PluginManifest manifest = PluginManifest::fromFile(manifestPath, &error);
            if (!manifest.isValid()) {
                emit pluginLoadFailed(manifestPath, error);
                continue;
            }
            if (findEntry(manifest.id()) != nullptr) {
                emit pluginLoadFailed(manifest.id(), QStringLiteral("Duplicate plugin package ID"));
                continue;
            }
            const bool duplicateSource = std::any_of(
                m_entries.cbegin(), m_entries.cend(), [&manifest](const auto &entry) {
                    return entry->spec.sourceId == manifest.sourceId();
                });
            if (duplicateSource) {
                emit pluginLoadFailed(manifest.id(), QStringLiteral("Duplicate plugin source ID"));
                continue;
            }

            auto entry = std::make_unique<Entry>();
            entry->manifest = manifest;
            entry->spec = {
                manifest.id(),
                manifest.sourceId(),
                manifest.name(),
                manifest.version(),
                manifest.category(),
                PluginState::Discovered,
                {},
                QFileInfo(manifestPath).absolutePath(),
                0,
            };
            const QString packageId = entry->spec.id;
            m_entries.push_back(std::move(entry));
            ++count;
            emit pluginChanged(packageId);
        }
    }
    return count;
}

bool PluginManager::load(const QString &packageId)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr) {
        return false;
    }
    if (entry->loader != nullptr) {
        return false;
    }

    QString error;
    if (!supportsRuntime(entry->manifest, &error)) {
        fail(*entry, error);
        return false;
    }
    if (!QLibrary::isLibrary(entry->manifest.libraryAbsolutePath())) {
        fail(*entry, QStringLiteral("Package library is not a loadable shared library"));
        return false;
    }

    auto loader = std::make_unique<QPluginLoader>(entry->manifest.libraryAbsolutePath());
    loader->setLoadHints({});
    QObject *instance = loader->instance();
    if (instance == nullptr) {
        fail(*entry, loader->errorString());
        return false;
    }
    if (entry->manifest.category() == PluginCategory::Source
        && qobject_cast<IMusicSourcePlugin *>(instance) == nullptr) {
        loader->unload();
        fail(*entry, QStringLiteral("Package does not implement IMusicSourcePlugin"));
        return false;
    }

    entry->loader = std::move(loader);
    entry->instance = instance;
    entry->spec.state = PluginState::Loaded;
    entry->spec.error.clear();
    emit pluginChanged(packageId);
    return entry->spec.state == PluginState::Loaded && entry->loader != nullptr;
}

bool PluginManager::failLoadedPlugin(const QString &packageId, const QString &error)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr || entry->loader == nullptr || entry->spec.activeLeases != 0) {
        return false;
    }
    if (!entry->loader->unload()) {
        fail(*entry, error + QStringLiteral(": ") + entry->loader->errorString());
        return false;
    }

    entry->loader.reset();
    entry->instance = nullptr;
    fail(*entry, error);
    return true;
}

PluginOperationResult PluginManager::unload(const QString &packageId)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr) {
        return PluginOperationResult::NotFound;
    }
    if (entry->spec.activeLeases != 0) {
        return PluginOperationResult::Busy;
    }
    if (entry->loader == nullptr) {
        return PluginOperationResult::Success;
    }
    if (!entry->loader->unload()) {
        fail(*entry, entry->loader->errorString());
        return PluginOperationResult::Failed;
    }

    entry->loader.reset();
    entry->instance = nullptr;
    entry->spec.state = PluginState::Unloaded;
    entry->spec.error.clear();
    emit pluginChanged(packageId);
    return PluginOperationResult::Success;
}

PluginOperationResult PluginManager::reload(const QString &packageId)
{
    const PluginOperationResult unloadResult = unload(packageId);
    if (unloadResult != PluginOperationResult::Success) {
        return unloadResult;
    }
    return load(packageId) ? PluginOperationResult::Success : PluginOperationResult::Failed;
}

PluginLease PluginManager::acquire(const QString &packageId)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr || entry->spec.state != PluginState::Loaded) {
        return {};
    }

    ++entry->spec.activeLeases;
    emit pluginChanged(packageId);
    const QPointer<PluginManager> manager(this);
    auto state = std::make_shared<PluginLease::State>();
    state->release = [manager, packageId] {
        if (manager != nullptr) {
            manager->releaseLease(packageId);
        }
    };
    return PluginLease(std::move(state));
}

PluginSpec PluginManager::plugin(const QString &packageId) const
{
    const Entry *entry = findEntry(packageId);
    return entry == nullptr ? PluginSpec{} : entry->spec;
}

QVariantList PluginManager::plugins() const
{
    QVariantList result;
    for (const auto &entry : m_entries) {
        const PluginSpec &spec = entry->spec;
        result.append(QVariantMap{
            {QStringLiteral("id"), spec.id},
            {QStringLiteral("sourceId"), spec.sourceId},
            {QStringLiteral("name"), spec.name},
            {QStringLiteral("version"), spec.version},
            {QStringLiteral("category"), pluginCategoryName(spec.category)},
            {QStringLiteral("state"), pluginStateName(spec.state)},
            {QStringLiteral("error"), spec.error},
            {QStringLiteral("path"), spec.path},
            {QStringLiteral("activeLeases"), spec.activeLeases},
            {QStringLiteral("reloadable"), spec.activeLeases == 0},
        });
    }
    return result;
}

QObject *PluginManager::pluginInstance(const QString &packageId) const
{
    const Entry *entry = findEntry(packageId);
    return entry == nullptr ? nullptr : entry->instance;
}

void PluginManager::discoverPlugins()
{
    discover();
}

bool PluginManager::loadPlugin(const QString &packageId)
{
    return load(packageId);
}

bool PluginManager::unloadPlugin(const QString &packageId)
{
    return unload(packageId) == PluginOperationResult::Success;
}

bool PluginManager::reloadPlugin(const QString &packageId)
{
    return reload(packageId) == PluginOperationResult::Success;
}

PluginManager::Entry *PluginManager::findEntry(const QString &packageId)
{
    const auto iterator = std::find_if(m_entries.begin(), m_entries.end(),
                                       [&packageId](const auto &entry) {
                                           return entry->spec.id == packageId;
                                       });
    return iterator == m_entries.end() ? nullptr : iterator->get();
}

const PluginManager::Entry *PluginManager::findEntry(const QString &packageId) const
{
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(),
                                       [&packageId](const auto &entry) {
                                           return entry->spec.id == packageId;
                                       });
    return iterator == m_entries.cend() ? nullptr : iterator->get();
}

bool PluginManager::supportsRuntime(const PluginManifest &manifest, QString *error) const
{
    if (manifest.minimumHostPluginApiMinor() > hostPluginApiMinor) {
        *error = QStringLiteral("Package requires a newer host plugin API");
        return false;
    }
    if (manifest.requiredQtMajor() != 0 && manifest.requiredQtMajor() != QT_VERSION_MAJOR) {
        *error = QStringLiteral("Package requires a different Qt major version");
        return false;
    }
    if (!manifest.requiredArchitecture().isEmpty()
        && manifest.requiredArchitecture() != QSysInfo::currentCpuArchitecture()) {
        *error = QStringLiteral("Package requires architecture %1")
                     .arg(manifest.requiredArchitecture());
        return false;
    }
    if (!manifest.requiredBuildMode().isEmpty()
        && manifest.requiredBuildMode() != hostBuildMode()) {
        *error = QStringLiteral("Package requires build mode %1")
                     .arg(manifest.requiredBuildMode());
        return false;
    }
    return true;
}

void PluginManager::fail(Entry &entry, const QString &error)
{
    entry.spec.state = PluginState::Failed;
    entry.spec.error = error;
    emit pluginLoadFailed(entry.spec.id, error);
    emit pluginChanged(entry.spec.id);
}

void PluginManager::releaseLease(const QString &packageId)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr || entry->spec.activeLeases == 0) {
        return;
    }

    --entry->spec.activeLeases;
    emit pluginChanged(packageId);
}
