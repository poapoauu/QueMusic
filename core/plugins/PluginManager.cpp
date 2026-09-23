#include "PluginManager.h"

#include "PluginManifest.h"
#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QPointer>
#include <QSysInfo>

#include <algorithm>
#include <functional>
#include <map>

namespace {

constexpr int hostPluginApiMinor = 0;

auto &pinnedPackages()
{
    // Intentionally never destroyed, including during static teardown: a pin
    // owns the loader/root until the OS reclaims the process. Keying by the
    // library coalesces violations and also protects replacement managers.
    static auto *packages = new std::map<QString, std::shared_ptr<QPluginLoader>>;
    return *packages;
}

QString restartRequiredReason()
{
    return QStringLiteral("source.external-destruction.restart-required");
}

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

QString hostBuildKey()
{
    return QStringLiteral(QUEMUSIC_PLUGIN_BUILD_KEY);
}

QString hostPluginArchitecture()
{
    return canonicalPluginArchitecture(QSysInfo::buildCpuArchitecture());
}

}

struct PluginLease::State {
    std::shared_ptr<QPluginLoader> loader;
    QString libraryPath;
    std::function<void()> release;
    std::function<void()> notifyPin;

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
    std::shared_ptr<QPluginLoader> loader;
    QObject *instance = nullptr;
    QString libraryPath;

    bool isPinned() const
    {
        return pinnedPackages().find(libraryPath) != pinnedPackages().end();
    }
};

PluginLease::PluginLease(std::shared_ptr<State> state)
    : m_state(std::move(state))
{
}

bool PluginLease::isValid() const
{
    return m_state != nullptr;
}

void PluginLease::pinLoadedPackage() const
{
    // Notifications can delete the caller and release its lease reentrantly.
    const auto state = m_state;
    if (state && state->loader
        && pinnedPackages().emplace(state->libraryPath, state->loader).second
        && state->notifyPin) {
        state->notifyPin();
    }
}

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    connect(this, &PluginManager::pluginChanged, this, &PluginManager::pluginsChanged);
}

PluginManager::~PluginManager()
{
    *m_callable = false;
    // Entries and callable leases share the loader. Its final healthy owner
    // unloads it; R7 alone transfers an additional process-lifetime reference.
}

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
            const QFileInfo library(manifest.libraryAbsolutePath());
            entry->libraryPath = library.canonicalFilePath();
            if (entry->libraryPath.isEmpty()) {
                entry->libraryPath = library.absoluteFilePath();
            }
            entry->spec = {
                manifest.id(),
                manifest.sourceId(),
                manifest.name(),
                manifest.version(),
                manifest.category(),
                PluginState::Discovered,
                {},
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
    if (entry == nullptr || entry->isPinned()) {
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

    auto loader = std::shared_ptr<QPluginLoader>(new QPluginLoader, [](QPluginLoader *loader) {
        if (loader->isLoaded()) loader->unload();
        delete loader;
    });
    loader->setLoadHints({});
    loader->setFileName(entry->manifest.libraryAbsolutePath());
    if (entry->manifest.category() == PluginCategory::Source) {
        const QString metadataIid =
            loader->metaData().value(QStringLiteral("IID")).toString();
        if (metadataIid != entry->manifest.sourceInterfaceId()) {
            fail(*entry,
                 QStringLiteral("Package plugin metadata IID %1 does not match manifest interface %2")
                     .arg(metadataIid, entry->manifest.sourceInterfaceId()));
            return false;
        }
    }
    if (!loader->load()) {
        fail(*entry, loader->errorString());
        return false;
    }
    QObject *instance = loader->instance();
    if (instance == nullptr) {
        fail(*entry, loader->errorString());
        return false;
    }
    if (entry->manifest.category() == PluginCategory::Source) {
        const int sourceSdkAbi = entry->manifest.sourceSdkAbi();
        if (sourceSdkAbi == 2 && qobject_cast<IMusicSourcePluginV2 *>(instance) == nullptr) {
            loader->unload();
            fail(*entry, QStringLiteral("Package does not implement IMusicSourcePluginV2"));
            return false;
        }
    }

    auto *uiProvider = qobject_cast<ISourceManagementUiProvider *>(instance);
    if (entry->manifest.hasManagementUi() && uiProvider == nullptr) {
        loader->unload();
        fail(*entry, QStringLiteral("Package declares management UI but does not implement ISourceManagementUiProvider"));
        return false;
    }
    if (!entry->manifest.hasManagementUi() && uiProvider != nullptr) {
        loader->unload();
        fail(*entry, QStringLiteral("Package implements ISourceManagementUiProvider without manifest declaration"));
        return false;
    }
    if (uiProvider != nullptr) {
        const ManagementUiDescriptor ui = uiProvider->managementUi();
        const QString path = ui.componentUrl.toString();
        if (ui.uiApiVersion != entry->manifest.pluginUiApiVersion()
            || ui.componentUrl.isEmpty() || !ui.componentUrl.isRelative()
            || ui.componentUrl.hasQuery() || ui.componentUrl.hasFragment()
            || path != entry->manifest.managementUiRelativePath()
            || (!ui.supportsCreate && !ui.supportsEdit)) {
            loader->unload();
            fail(*entry, QStringLiteral("Package management UI provider does not match manifest"));
            return false;
        }
    }

    entry->loader = std::move(loader);
    entry->instance = instance;
    entry->spec.state = PluginState::Loaded;
    entry->spec.error.clear();
    entry->spec.busyReason.clear();
    emit pluginChanged(packageId);
    return entry->spec.state == PluginState::Loaded && entry->loader != nullptr;
}

bool PluginManager::failLoadedPlugin(const QString &packageId, const QString &error)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr || entry->isPinned() || entry->loader == nullptr
        || entry->spec.activeLeases != 0) {
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
    if (entry->isPinned()) {
        return PluginOperationResult::Busy;
    }
    if (entry->spec.activeLeases != 0) {
        entry->spec.busyReason = QStringLiteral("source.sessions.active");
        emit pluginChanged(packageId);
        return PluginOperationResult::Busy;
    }
    if (entry->loader == nullptr) {
        return PluginOperationResult::Success;
    }
    if (!entry->loader->isLoaded()) {
        entry->loader.reset();
        entry->instance = nullptr;
        entry->spec.state = PluginState::Unloaded;
        entry->spec.error.clear();
        entry->spec.busyReason.clear();
        emit pluginChanged(packageId);
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
    entry->spec.busyReason.clear();
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
    if (entry == nullptr || entry->isPinned() || entry->spec.state != PluginState::Loaded) {
        return {};
    }

    ++entry->spec.activeLeases;
    const QPointer<PluginManager> manager(this);
    const std::weak_ptr<bool> callable = m_callable;
    auto state = std::make_shared<PluginLease::State>();
    state->loader = entry->loader;
    state->libraryPath = entry->libraryPath;
    state->release = [manager, callable, packageId] {
        const auto live = callable.lock();
        if (live && *live && manager != nullptr) {
            manager->releaseLease(packageId);
        }
    };
    state->notifyPin = [manager, callable, packageId] {
        const auto live = callable.lock();
        if (live && *live && manager != nullptr) emit manager->pluginChanged(packageId);
    };
    emit pluginChanged(packageId);
    // An existing session can be externally deleted by an acquisition observer.
    if (manager == nullptr || entry->isPinned()) {
        return {};
    }
    return PluginLease(std::move(state));
}

void PluginManager::pinLoadedPackage(const QString &packageId)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr || entry->isPinned() || entry->loader == nullptr) {
        return;
    }
    // Transfer ownership before emitting anything that could destroy this manager.
    pinnedPackages().emplace(entry->libraryPath, std::move(entry->loader));
    emit pluginChanged(packageId);
}

PluginSpec PluginManager::plugin(const QString &packageId) const
{
    const Entry *entry = findEntry(packageId);
    PluginSpec spec = entry == nullptr ? PluginSpec{} : entry->spec;
    if (entry != nullptr && entry->isPinned()) {
        spec.busyReason = restartRequiredReason();
    }
    return spec;
}

QVariantList PluginManager::plugins() const
{
    QVariantList result;
    for (const auto &entry : m_entries) {
        const PluginSpec &spec = entry->spec;
        const bool pinned = entry->isPinned();
        result.append(QVariantMap{
            {QStringLiteral("id"), spec.id},
            {QStringLiteral("sourceId"), spec.sourceId},
            {QStringLiteral("name"), spec.name},
            {QStringLiteral("version"), spec.version},
            {QStringLiteral("category"), pluginCategoryName(spec.category)},
            {QStringLiteral("state"), pluginStateName(spec.state)},
            {QStringLiteral("error"), spec.error},
            {QStringLiteral("busyReason"), pinned ? restartRequiredReason() : spec.busyReason},
            {QStringLiteral("path"), spec.path},
            {QStringLiteral("activeLeases"), spec.activeLeases},
            {QStringLiteral("loadable"), !pinned && entry->loader == nullptr},
            {QStringLiteral("unloadable"), !pinned && entry->loader != nullptr
                 && spec.activeLeases == 0},
            {QStringLiteral("reloadable"), !pinned && spec.state == PluginState::Loaded
                 && spec.activeLeases == 0},
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
    const QString hostArchitecture = hostPluginArchitecture();
    if (manifest.minimumHostPluginApiMinor() > hostPluginApiMinor) {
        *error = QStringLiteral("Package requires a newer host plugin API");
        return false;
    }
    if (manifest.requiredQtMajor() != 0 && manifest.requiredQtMajor() != QT_VERSION_MAJOR) {
        *error = QStringLiteral("Package requires a different Qt major version");
        return false;
    }
    if (!manifest.requiredArchitecture().isEmpty()
        && !isPluginArchitectureCompatible(manifest.requiredArchitecture(),
                                           hostArchitecture)) {
        *error = QStringLiteral("Package requires architecture %1; host is %2")
                     .arg(manifest.requiredArchitecture(), hostArchitecture);
        return false;
    }
    if (!manifest.requiredBuildKey().isEmpty()
        && manifest.requiredBuildKey() != hostBuildKey()) {
        *error = QStringLiteral("Package requires build key %1")
                     .arg(manifest.requiredBuildKey());
        return false;
    }
    return true;
}

void PluginManager::fail(Entry &entry, const QString &error)
{
    entry.spec.state = PluginState::Failed;
    entry.spec.error = error;
    entry.spec.busyReason.clear();
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
    if (entry->spec.activeLeases == 0) {
        entry->spec.busyReason.clear();
    }
    emit pluginChanged(packageId);
}
